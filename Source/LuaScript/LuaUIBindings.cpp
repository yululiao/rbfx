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
        using RBFX_THIS = UIElement;
        RBFX_USERTYPE(UIElement,
            sol::no_constructor
            RBFX_BASES(Serializable, Object)
            // sol3 resolves base-class members only one level deep, so widgets
            // derived from BorderImage must list the full chain to also reach
            // UIElement's methods (02_HelloGUI SetMinWidth, 16_Chat SetStyleAuto).
            RBFX_RAW(SetName, [](UIElement* element, const char* name) { if (element) element->SetName(name); })
            RBFX_RAW(GetName, [](UIElement* element) -> std::string { return element ? element->GetName().c_str() : ""; })
            RBFX_OVERLOAD(SetPosition,
                RBFX_CAST(SetPosition, void, const IntVector2&),
                RBFX_CAST(SetPosition, void, int, int),
                // Lua arithmetic (e.g. width / 2) yields floats; accept them here
                [](UIElement* element, double x, double y) {
                    if (element)
                        element->SetPosition(static_cast<int>(x), static_cast<int>(y));
                })
            RBFX_M(GetPosition)
            RBFX_OVERLOAD(SetSize,
                RBFX_CAST(SetSize, void, const IntVector2&),
                RBFX_CAST(SetSize, void, int, int),
                [](UIElement* element, double w, double h) {
                    if (element)
                        element->SetSize(static_cast<int>(w), static_cast<int>(h));
                })
            RBFX_M(GetSize)
            RBFX_M(SetWidth)
            RBFX_M(SetHeight)
            RBFX_M(SetFixedWidth)
            RBFX_M(SetFixedHeight)
            RBFX_OVERLOAD(SetFixedSize,
                RBFX_CAST(SetFixedSize, void, const IntVector2&),
                RBFX_CAST(SetFixedSize, void, int, int),
                [](UIElement* element, double w, double h) {
                    if (element)
                        element->SetFixedSize(static_cast<int>(w), static_cast<int>(h));
                })
            RBFX_M(GetWidth)
            RBFX_M(GetHeight)
            RBFX_OVERLOAD(SetMinSize,
                RBFX_CAST(SetMinSize, void, const IntVector2&),
                RBFX_CAST(SetMinSize, void, int, int),
                [](UIElement* element, double w, double h) {
                    if (element)
                        element->SetMinSize(static_cast<int>(w), static_cast<int>(h));
                })
            RBFX_OVERLOAD(SetMaxSize,
                RBFX_CAST(SetMaxSize, void, const IntVector2&),
                RBFX_CAST(SetMaxSize, void, int, int),
                [](UIElement* element, double w, double h) {
                    if (element)
                        element->SetMaxSize(static_cast<int>(w), static_cast<int>(h));
                })
            // Lua arithmetic on GetRowWidth() results yields floats; accept them.
            RBFX_RAW(SetMinWidth, [](UIElement* element, double width) {
                if (element)
                    element->SetMinWidth(static_cast<int>(width));
            })
            RBFX_RAW(SetMinHeight, [](UIElement* element, double height) {
                if (element)
                    element->SetMinHeight(static_cast<int>(height));
            })
            RBFX_RAW(SetMaxWidth, [](UIElement* element, double width) {
                if (element)
                    element->SetMaxWidth(static_cast<int>(width));
            })
            RBFX_RAW(SetMaxHeight, [](UIElement* element, double height) {
                if (element)
                    element->SetMaxHeight(static_cast<int>(height));
            })
            RBFX_M(SetDefaultStyle)
            RBFX_RAW(SetAlignment, [](UIElement* element, int hAlign, int vAlign) {
                if (element)
                    element->SetAlignment(static_cast<HorizontalAlignment>(hAlign), static_cast<VerticalAlignment>(vAlign));
            })
            RBFX_RAW(SetHorizontalAlignment, [](UIElement* element, int align) {
                if (element)
                    element->SetHorizontalAlignment(static_cast<HorizontalAlignment>(align));
            })
            RBFX_RAW(SetVerticalAlignment, [](UIElement* element, int align) {
                if (element)
                    element->SetVerticalAlignment(static_cast<VerticalAlignment>(align));
            })
            RBFX_RAW(SetColor, [](UIElement* element, const Color& color) {
                if (element)
                    element->SetColor(color);
            })
            RBFX_M(SetOpacity)
            RBFX_M(SetUseDerivedOpacity)
            RBFX_M(SetVisible)
            RBFX_M(IsVisible)
            RBFX_M(SetEnabled)
            RBFX_M(SetFocus)
            RBFX_M(SetBringToFront)
            RBFX_RAW(SetLayout, [](UIElement* element, int mode, sol::optional<int> spacing, sol::optional<IntRect> border) {
                if (element)
                    element->SetLayout(static_cast<LayoutMode>(mode), spacing.value_or(0), border.value_or(IntRect::ZERO));
            })
            RBFX_M(SetLayoutSpacing)
            RBFX_M(SetLayoutBorder)
            RBFX_RAW(SetLayoutMode, [](UIElement* element, int mode) {
                if (element)
                    element->SetLayoutMode(static_cast<LayoutMode>(mode));
            })
            RBFX_RAW(SetStyle, [](UIElement* element, const char* styleName) -> bool {
                return element && element->SetStyle(styleName);
            })
            RBFX_RAW(SetStyleAuto, [](UIElement* element) -> bool {
                return element && element->SetStyleAuto();
            })
            RBFX_RAW(CreateChild, [](UIElement* element, const char* typeName, sol::optional<const char*> name,
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
            RBFX_M(AddChild)
            RBFX_RAW(RemoveChild, [](UIElement* parent, UIElement* child) {
                if (parent && child)
                    parent->RemoveChild(child);
            })
            RBFX_M(RemoveAllChildren)
            RBFX_RAW(GetChild, [](UIElement* element, const char* name, sol::optional<bool> recursive,
                sol::this_state s) -> sol::object {
                if (!element)
                    return sol::lua_nil;
                return WrapLuaObject(sol::state_view(s), element->GetChild(name, recursive.value_or(false)));
            })
            RBFX_RAW(GetChildren, [](UIElement* element, sol::this_state s) -> sol::table {
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
            RBFX_M(GetParent)
            RBFX_RAW(SetVar, [](UIElement* element, const char* key, sol::object value, sol::this_state s) {
                if (element)
                    element->SetVar(key, LuaToVariant(sol::state_view(s), value));
            })
            RBFX_RAW(GetVar, [](UIElement* element, const char* key, sol::this_state s) -> sol::object {
                return element ? VariantToLua(sol::state_view(s), element->GetVar(key)) : sol::lua_nil;
            })
            // Manual layout refresh and z-order control (37_UIDrag).
            RBFX_M(BringToFront)
            RBFX_M(UpdateLayout)
            RBFX_M(HasFocus)
            RBFX_M(GetMinWidth)
            // Tags for grouping elements (37_UIDrag drag sources).
            RBFX_RAW(AddTag, [](UIElement* element, const char* tag) {
                if (element)
                    element->AddTag(tag);
            })
            RBFX_M(HasTag)
            RBFX_RAW(GetChildrenWithTag, [](UIElement* element, const char* tag, sol::optional<bool> recursive,
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
            RBFX_RAW(SetFocusMode, [](UIElement* element, int mode) {
                if (element)
                    element->SetFocusMode(static_cast<FocusMode>(mode));
            })
            // Render order within the parent (37_UIDrag windows above sprites).
            RBFX_M(SetPriority)
            // Object lifetime hints.
            RBFX_M(SetTemporary)
            // Attribute animation, e.g. animating Text's "Text" attribute
            // (30_LightAnimation).
            RBFX_RAW(SetAttributeAnimation, [](UIElement* element, const char* name,
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
        using RBFX_THIS = Font;
        RBFX_USERTYPE(Font,
            sol::no_constructor
            RBFX_BASES(Resource, Object)
        );
    }
    RegisterLuaObjectWrapper<Font>();

    // Text label. Constructible standalone so it can be filled into
    // ListView/DropDownList item slots (47_Typography, 54_WindowSettings).
    {
        using RBFX_THIS = Text;
        RBFX_USERTYPE(Text,
            sol::call_constructor, sol::factories([context]() {
                return SharedPtr<Text>(new Text(context));
            })
            RBFX_BASES(UIElement, Serializable, Object)
            RBFX_RAW(SetText, [](Text* text, const char* value) {
                if (text)
                    text->SetText(value);
            })
            RBFX_OVERLOAD(SetFont,
                [](Text* text, Font* font) -> bool { return text && text->SetFont(font); },
                [](Text* text, Font* font, float size) -> bool { return text && text->SetFont(font, size); },
                [](Text* text, const char* fontName, float size) -> bool { return text && text->SetFont(fontName, size); })
            RBFX_M(SetFontSize)
            RBFX_RAW(SetTextAlignment, [](Text* text, int align) {
                if (text)
                    text->SetTextAlignment(static_cast<HorizontalAlignment>(align));
            })
            RBFX_M(SetRowSpacing)
            RBFX_M(SetWordwrap)
            RBFX_M(GetNumRows)
            RBFX_M(GetRowHeight)
            RBFX_M(GetRowWidth)
            // Text effects (35_SignedDistanceFieldText).
            RBFX_RAW(SetTextEffect, [](Text* text, int effect) {
                if (text)
                    text->SetTextEffect(static_cast<TextEffect>(effect));
            })
            RBFX_M(SetEffectColor)
            RBFX_M(SetEffectDepthBias)
            // Localization of the displayed string (40_Localization).
            RBFX_M(SetAutoLocalizable)
        );
    }
    RegisterLuaObjectWrapper<Text>();

    // Text effect constants.
    RBFX_ENUM_TABLE(TE, "NONE", TE_NONE, "SHADOW", TE_SHADOW, "STROKE", TE_STROKE);

    // BorderImage: textured widget base.
    {
        using RBFX_THIS = BorderImage;
        RBFX_USERTYPE(BorderImage,
            sol::no_constructor
            RBFX_BASES(UIElement, Serializable, Object)
            RBFX_M(SetTexture)
            RBFX_M(GetTexture)
            RBFX_RAW(SetBlendMode, [](BorderImage* image, int mode) {
                if (image)
                    image->SetBlendMode(static_cast<BlendMode>(mode));
            })
            // Sub-rectangle of the texture to display (49/50 UI backgrounds).
            RBFX_M(SetImageRect)
            RBFX_M(SetFullImageRect)
        );
    }
    RegisterLuaObjectWrapper<BorderImage>();

    // Button: styled through SetStyle("Button") + released/pressed events.
    {
        using RBFX_THIS = Button;
        RBFX_USERTYPE(Button,
            sol::no_constructor
            RBFX_BASES(BorderImage, UIElement, Serializable, Object)
        );
    }
    RegisterLuaObjectWrapper<Button>();

    // LineEdit: single-line text input with TextFinished event (16_Chat).
    // Registered after BorderImage, which its LuaBases chain requires.
    {
        using RBFX_THIS = LineEdit;
        RBFX_USERTYPE(LineEdit,
            sol::no_constructor
            RBFX_BASES(BorderImage, UIElement, Serializable, Object)
            RBFX_RAW(SetText, [](LineEdit* edit, const char* value) {
                if (edit)
                    edit->SetText(value);
            })
            RBFX_RAW(GetText, [](LineEdit* edit) -> const char* { return edit ? edit->GetText().c_str() : ""; })
        );
    }
    RegisterLuaObjectWrapper<LineEdit>();

    // Window: draggable container with optional modality.
    {
        using RBFX_THIS = Window;
        RBFX_USERTYPE(Window,
            sol::no_constructor
            RBFX_BASES(BorderImage, UIElement, Serializable, Object)
            RBFX_M(SetModal)
            RBFX_M(SetMovable)
        );
    }
    RegisterLuaObjectWrapper<Window>();

    // CheckBox: two-state toggle widget (14_SoundEffects).
    {
        using RBFX_THIS = CheckBox;
        RBFX_USERTYPE(CheckBox,
            sol::no_constructor
            RBFX_BASES(BorderImage, UIElement, Serializable, Object)
            RBFX_M(SetChecked)
            RBFX_M(IsChecked)
        );
    }
    RegisterLuaObjectWrapper<CheckBox>();

    // Slider: float value in a range, fires SliderChanged events.
    {
        using RBFX_THIS = Slider;
        RBFX_USERTYPE(Slider,
            sol::no_constructor
            RBFX_BASES(BorderImage, UIElement, Serializable, Object)
            RBFX_M(SetRange)
            RBFX_M(SetValue)
            RBFX_M(GetValue)
        );
    }
    RegisterLuaObjectWrapper<Slider>();

    // DropDownList: item picker with a popup list (14_SoundEffects).
    {
        using RBFX_THIS = DropDownList;
        RBFX_USERTYPE(DropDownList,
            sol::no_constructor
            RBFX_BASES(Button, BorderImage, UIElement, Serializable, Object)
            RBFX_M(AddItem)
            RBFX_M(RemoveAllItems)
            RBFX_RAW(SetSelection, [](DropDownList* list, double index) {
                if (list)
                    list->SetSelection(static_cast<unsigned>(index));
            })
            RBFX_M(GetSelection)
            RBFX_M(GetNumItems)
            RBFX_M(GetItem)
            RBFX_M(GetSelectedItem)
        );
    }
    RegisterLuaObjectWrapper<DropDownList>();

    // ListView: scrollable item list (47_Typography, 54_WindowSettings).
    {
        using RBFX_THIS = ListView;
        RBFX_USERTYPE(ListView,
            sol::no_constructor
            RBFX_BASES(UIElement, Serializable, Object)
            RBFX_M(AddItem)
            RBFX_M(RemoveAllItems)
            RBFX_RAW(SetSelection, [](ListView* list, double index) {
                if (list)
                    list->SetSelection(static_cast<unsigned>(index));
            })
            RBFX_M(GetSelection)
            RBFX_M(GetNumItems)
            RBFX_M(GetItem)
            RBFX_RAW(SetHighlightMode, [](ListView* list, int mode) {
                if (list)
                    list->SetHighlightMode(static_cast<HighlightMode>(mode));
            })
            RBFX_M(SetSelectOnClickEnd)
        );
    }
    RegisterLuaObjectWrapper<ListView>();

    // ToolTip: hover help container attached to a widget (48_Hello3DUI).
    {
        using RBFX_THIS = ToolTip;
        RBFX_USERTYPE(ToolTip,
            sol::no_constructor
            RBFX_BASES(UIElement, Serializable, Object)
        );
    }
    RegisterLuaObjectWrapper<ToolTip>();

    // Sprite: textured quad for 2D overlay work (18_Urho2DSprite).
    {
        using RBFX_THIS = Sprite;
        RBFX_USERTYPE(Sprite,
            sol::no_constructor
            RBFX_BASES(UIElement, Serializable, Object)
            RBFX_M(SetTexture)
            RBFX_M(SetImageRect)
            RBFX_RAW(SetBlendMode, [](Sprite* sprite, int mode) {
                if (sprite)
                    sprite->SetBlendMode(static_cast<BlendMode>(mode));
            })
            RBFX_OVERLOAD(SetHotSpot,
                RBFX_CAST(SetHotSpot, void, const IntVector2&),
                RBFX_CAST(SetHotSpot, void, int, int),
                [](Sprite* sprite, double x, double y) {
                    if (sprite)
                        sprite->SetHotSpot(static_cast<int>(x), static_cast<int>(y));
                })
            RBFX_OVERLOAD(SetScale,
                RBFX_CAST(SetScale, void, const Vector2&),
                RBFX_CAST(SetScale, void, float, float),
                RBFX_CAST(SetScale, void, float))
            RBFX_M(SetRotation)
            RBFX_M(GetRotation)
        );
    }
    RegisterLuaObjectWrapper<Sprite>();

    // Cursor: OS/software cursor element (08_Decals and friends).
    // Constructed explicitly and installed through UI:SetCursor when the
    // sample needs to toggle cursor visibility (17_SceneReplication).
    {
        using RBFX_THIS = Cursor;
        RBFX_USERTYPE(Cursor,
            sol::call_constructor, sol::factories([context]() {
                return SharedPtr<Cursor>(new Cursor(context));
            })
            RBFX_BASES(BorderImage, UIElement, Serializable, Object)
        );
    }
    RegisterLuaObjectWrapper<Cursor>();

    // UI subsystem: root element access and global scale.
    {
        using RBFX_THIS = UI;
        RBFX_USERTYPE(UI,
            sol::no_constructor
            RBFX_BASES(Object)
            RBFX_M(GetRoot)
            RBFX_M(GetRootModalElement)
            RBFX_M(GetFocusElement)
            RBFX_M(GetCursor)
            RBFX_M(SetCursor)
            RBFX_M(GetUICursorPosition)
            RBFX_RAW(GetElementAt, [](UI* ui, const IntVector2& position, sol::optional<bool> enabledOnly) {
                return ui ? ui->GetElementAt(position, enabledOnly.value_or(true)) : nullptr;
            })
            RBFX_M(ConvertUIToSystem)
            RBFX_M(SetScale)
            RBFX_M(SetUseSystemClipboard)
            // Font rendering configuration (47_Typography).
            RBFX_M(SetForceAutoHint)
            RBFX_M(GetForceAutoHint)
            RBFX_RAW(SetFontHintLevel, [](UI* ui, int level) {
                if (ui)
                    ui->SetFontHintLevel(static_cast<FontHintLevel>(level));
            })
            RBFX_RAW(GetFontHintLevel, [](UI* ui) {
                return ui ? static_cast<int>(ui->GetFontHintLevel()) : 0;
            })
            RBFX_M(SetFontSubpixelThreshold)
            RBFX_M(GetFontSubpixelThreshold)
            RBFX_M(SetFontOversampling)
            RBFX_M(GetFontOversampling)
            // Immediate-mode debug outline of the element hierarchy
            // (48_Hello3DUI).
            RBFX_M(DebugDraw)
            // Instantiate a widget tree from a layout XML file (38_SceneAndUILoad).
            RBFX_RAW(LoadLayout, [](UI* ui, XMLFile* file, sol::optional<XMLFile*> styleFile,
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
        using RBFX_THIS = Console;
        RBFX_USERTYPE(Console,
            sol::no_constructor
            RBFX_BASES(Object)
            RBFX_M(SetVisible)
            RBFX_M(Toggle)
            RBFX_M(IsVisible)
            RBFX_RAW(SetCommandInterpreter, [](Console* console, const char* name) {
                if (console)
                    console->SetCommandInterpreter(name);
            })
            RBFX_M(SetAutoVisibleOnError)
        );
    }
    RegisterLuaObjectWrapper<Console>();

    // UIComponent: renders a UI subtree into 3D space on a material
    // (48_Hello3DUI).
    {
        using RBFX_THIS = UIComponent;
        RBFX_USERTYPE(UIComponent,
            sol::no_constructor
            RBFX_BASES(Component, Serializable, Object)
            RBFX_M(GetRoot)
            RBFX_M(GetMaterial)
        );
    }
    RegisterLuaObjectWrapper<UIComponent>();

    // Keyboard focus modes for UIElement:SetFocusMode.
    RBFX_ENUM_TABLE(FM, "NOTFOCUSABLE", FM_NOTFOCUSABLE, "RESETFOCUS", FM_RESETFOCUS, "FOCUSABLE",
        FM_FOCUSABLE, "FOCUSABLE_DEFOCUSABLE", FM_FOCUSABLE_DEFOCUSABLE);

    // ListView highlight modes (48_Hello3DUI).
    RBFX_ENUM_TABLE(HM, "NEVER", HM_NEVER, "FOCUS", HM_FOCUS, "ALWAYS", HM_ALWAYS);

    // FreeType hinting levels for UI:SetFontHintLevel (47_Typography).
    RBFX_ENUM_TABLE(FHL, "NONE", FONT_HINT_LEVEL_NONE, "LIGHT", FONT_HINT_LEVEL_LIGHT, "NORMAL",
        FONT_HINT_LEVEL_NORMAL);

    // Alignment and layout constants used by the setters above.
    RBFX_ENUM_TABLE(HA, "LEFT", HA_LEFT, "CENTER", HA_CENTER, "RIGHT", HA_RIGHT);

    RBFX_ENUM_TABLE(VA, "TOP", VA_TOP, "CENTER", VA_CENTER, "BOTTOM", VA_BOTTOM);

    RBFX_ENUM_TABLE(LM, "FREE", LM_FREE, "HORIZONTAL", LM_HORIZONTAL, "VERTICAL", LM_VERTICAL);

    // Global UI root accessor mirroring the tolua-era LuaSamples helper of
    // the same name; most samples' CreateGUI uses it.
    lua.set_function("GetUIRoot", [context](sol::this_state s) -> sol::object {
        auto* ui = context->GetSubsystem<UI>();
        if (!ui || !ui->GetRoot())
            return sol::lua_nil;
        return WrapLuaObject(sol::state_view(s), ui->GetRoot());
    });

    // Blend modes for BorderImage and Sprite widgets (RenderAPIDefs.h).
    RBFX_ENUM_TABLE(BLEND, "REPLACE", BLEND_REPLACE, "ADD", BLEND_ADD, "MULTIPLY", BLEND_MULTIPLY,
        "ALPHA", BLEND_ALPHA, "ADDALPHA", BLEND_ADDALPHA, "PREMULALPHA", BLEND_PREMULALPHA,
        "INVDESTALPHA", BLEND_INVDESTALPHA, "SUBTRACT", BLEND_SUBTRACT, "SUBTRACTALPHA",
        BLEND_SUBTRACTALPHA, "DEFERRED_DECAL", BLEND_DEFERRED_DECAL);
}

} // namespace Urho3D
