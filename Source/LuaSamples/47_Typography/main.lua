-- LuaSamples/47_Typography/main.lua
-- Lua port of Source/Samples/47_Typography: renders a text size sweep and
-- exposes UI font rendering controls (ForceAutoHint, FontHintLevel,
-- SubpixelThreshold, Oversampling) plus a white/black background toggle.

local TEXT_TAG = "Typography_text_tag"

local app = Sample:new()

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.FREE)
    input:SetMouseVisible(true)

    local ui = GetSubsystem("UI")
    local root = GetUIRoot()
    root:SetDefaultStyle(GetResource("XMLFile", "UI/DefaultStyle.xml"))

    -- Container for all content (root is owned by the sample framework)
    self.uielement = root:CreateChild("UIElement")
    self.uielement:SetAlignment(HA.CENTER, VA.CENTER)
    self.uielement:SetLayout(LM.VERTICAL, 10, IntRect(20, 40, 20, 40))

    -- Sample text
    self:CreateText()

    -- Checkbox to toggle the background color
    self:CreateCheckbox("White background", function(data) self:HandleWhiteBackground(data) end)
        :SetChecked(false)

    -- Global ForceAutoHint setting (affects character spacing)
    self:CreateCheckbox("UI::SetForceAutoHint", function(data) self:HandleForceAutoHint(data) end)
        :SetChecked(ui:GetForceAutoHint())

    -- Drop-down menu for the font hinting level
    self:CreateMenu("UI::SetFontHintLevel",
        { "FONT_HINT_LEVEL_NONE", "FONT_HINT_LEVEL_LIGHT", "FONT_HINT_LEVEL_NORMAL" },
        function(data) self:HandleFontHintLevel(data) end)
        :SetSelection(ui:GetFontHintLevel())

    -- Drop-down menu for the subpixel threshold
    self:CreateMenu("UI::SetFontSubpixelThreshold",
        { "0", "3", "6", "9", "12", "15", "18", "21" },
        function(data) self:HandleFontSubpixel(data) end)
        :SetSelection(ui:GetFontSubpixelThreshold() / 3)

    -- Drop-down menu for oversampling
    self:CreateMenu("UI::SetFontOversampling",
        { "1", "2", "3", "4", "5", "6", "7", "8" },
        function(data) self:HandleFontOversampling(data) end)
        :SetSelection(ui:GetFontOversampling() - 1)
end

function app:CreateText()
    local container = self.uielement:CreateChild("UIElement")
    container:SetAlignment(HA.LEFT, VA.TOP)
    container:SetLayout(LM.VERTICAL)

    local font = GetResource("Font", "Fonts/BlueHighway.ttf")

    for size2x = 2, 36 do
        local size = size2x / 2
        local text = Text()
        text:SetText("The quick brown fox jumps over the lazy dog (" .. size .. "pt)")
        text:SetFont(font, size)
        text:AddTag(TEXT_TAG)
        container:AddChild(text)
    end
end

function app:CreateCheckbox(label, handler)
    local container = self.uielement:CreateChild("UIElement")
    container:SetAlignment(HA.LEFT, VA.TOP)
    container:SetLayout(LM.HORIZONTAL, 8)

    local box = container:CreateChild("CheckBox")
    box:SetStyleAuto()

    local text = Text()
    container:AddChild(text)
    text:SetText(label)
    text:SetStyleAuto()
    text:AddTag(TEXT_TAG)

    SubscribeToEvent(box, "Toggled", handler)
    return box
end

function app:CreateMenu(label, items, handler)
    local container = self.uielement:CreateChild("UIElement")
    container:SetAlignment(HA.LEFT, VA.TOP)
    container:SetLayout(LM.HORIZONTAL, 8)

    local text = Text()
    container:AddChild(text)
    text:SetText(label)
    text:SetStyleAuto()
    text:AddTag(TEXT_TAG)

    local list = container:CreateChild("DropDownList")
    list:SetStyleAuto()

    for _, itemName in ipairs(items) do
        local item = Text()
        list:AddItem(item)
        item:SetText(itemName)
        item:SetStyleAuto()
        item:SetMinWidth(item:GetRowWidth(0) + 10)
        item:AddTag(TEXT_TAG)
    end

    text:SetMaxWidth(text:GetRowWidth(0))

    SubscribeToEvent(list, "ItemSelected", handler)
    return list
end

function app:HandleWhiteBackground(data)
    local box = data.Element
    local checked = box:IsChecked()

    local fg, bg
    if checked then
        fg, bg = Color.BLACK, Color.WHITE
    else
        fg, bg = Color.WHITE, Color.BLACK
    end

    local renderer = GetSubsystem("Renderer")
    renderer:GetDefaultZone():SetFogColor(bg)

    local text = self.uielement:GetChildrenWithTag(TEXT_TAG, true)
    for i = 1, #text do
        text[i]:SetColor(fg)
    end
end

function app:HandleForceAutoHint(data)
    local box = data.Element
    GetSubsystem("UI"):SetForceAutoHint(box:IsChecked())
end

function app:HandleFontHintLevel(data)
    local list = data.Element
    GetSubsystem("UI"):SetFontHintLevel(list:GetSelection())
end

function app:HandleFontSubpixel(data)
    local list = data.Element
    GetSubsystem("UI"):SetFontSubpixelThreshold(list:GetSelection() * 3)
end

function app:HandleFontOversampling(data)
    local list = data.Element
    GetSubsystem("UI"):SetFontOversampling(list:GetSelection() + 1)
end

app:Run()
