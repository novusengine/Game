local UIButton = require("API/UI/Button")
local ItemUtil = require("API/Game/ItemUtil")

type Point2D = { x : number, y : number }
type TooltipLine = { lineLeftText : string, lineRightText : string, lineLeftTextTemplate : string, lineRightTextTemplate : string, lineLeftTextWidget : userdata,  lineRightTextWidget : userdata}

local tooltipPanel = nil
local tooltipPanelData = { }
local tooltipLines = { }

local function RegisterTemplates()
    UI.RegisterPanelTemplate("GameTooltipBackgroundPanel",
    {
        background = "Data/Texture/interface/tooltips/ui-tooltip-background.dds",
        color = vec3.new(0.2, 0.2, 0.2),
        cornerRadius = 5.0,
    });

    UI.RegisterTextTemplate("GameTooltipLargeText",
    {
        font = "Data/Fonts/OpenSans-Regular.ttf",
        size = 12.0,
        color = vec3.new(255.0/255.0, 255.0/255.0, 255.0/255.0),
        borderSize = 0.5,
        borderColor = vec3.new(0.15, 0.15, 0.15),
    });

    for k, v in ItemUtil.RarityNames do
        local rarityColor = ItemUtil.RarityColors[k]
        local textTemplateName = "GameTooltipLargeText-" .. v

        local b = bit32.band(rarityColor, 0xff)
        local g = bit32.band(bit32.rshift(rarityColor, 8), 0xff)
        local r = bit32.band(bit32.rshift(rarityColor, 16), 0xff)
        
        UI.RegisterTextTemplate(textTemplateName,
        {
            font = "Data/Fonts/OpenSans-Regular.ttf",
            size = 13.0,
            color = vec3.new(r/255.0, g/255.0, b/255.0),
            borderSize = 0.5,
            borderColor = vec3.new(0.15, 0.15, 0.15),
        });
    end

    UI.RegisterTextTemplate("GameTooltipMediumText",
    {
        font = "Data/Fonts/OpenSans-Regular.ttf",
        size = 10.5,
        color = vec3.new(255.0/255.0, 255.0/255.0, 255.0/255.0),
        borderSize = 0.5,
        borderColor = vec3.new(0.15, 0.15, 0.15),
    });
    
    UI.RegisterTextTemplate("GameTooltipMediumText-Green",
    {
        font = "Data/Fonts/OpenSans-Regular.ttf",
        size = 10.5,
        color = vec3.new(30.0/255.0, 255.0/255.0, 0.0/255.0),
        borderSize = 0.5,
        borderColor = vec3.new(0.15, 0.15, 0.15),
    });

    UI.RegisterTextTemplate("GameTooltipMediumText-Yellow",
    {
        font = "Data/Fonts/OpenSans-Regular.ttf",
        size = 10.5,
        color = vec3.new(255.0/255.0, 210.0/255.0, 0.0/255.0),
        borderSize = 0.5,
        borderColor = vec3.new(0.15, 0.15, 0.15),
    });

    UI.RegisterTextTemplate("GameTooltipSmallText",
    {
        font = "Data/Fonts/OpenSans-Light.ttf",
        size = 10.0,
        color = vec3.new(255.0/255.0, 255.0/255.0, 255.0/255.0),
        borderSize = 0.5,
        borderColor = vec3.new(0.2, 0.2, 0.2),
    });
end

local function RefreshGameTooltip()
    local tooltipCurrentWidth, tooltipCurrentHeight = 0, 3
    local lineHeightPadding = 3

    for k, v in tooltipLines do
        local linePosY = tooltipCurrentHeight
        local isDoubleLine = v.lineRightText ~= nil

        if v.lineLeftTextWidget == nil and v.lineLeftText:len() > 0  then
            v.lineLeftTextWidget = tooltipPanel:NewText(v.lineLeftText, 5, -linePosY, 0, v.lineLeftTextTemplate);
            v.lineLeftTextWidget:SetAnchor(0.0, 1.0);
            v.lineLeftTextWidget:SetRelativePoint(0.0, 1.0);
        end
        
        local textWidth, textHeight = UI.CalculateTextSize(v.lineLeftText, v.lineLeftTextTemplate)

        if isDoubleLine then
            if v.lineRightTextWidget == nil and v.lineRightText:len() > 0 then 
                v.lineRightTextWidget = tooltipPanel:NewText(v.lineRightText, -5, -linePosY, 0, v.lineRightTextTemplate);
                v.lineRightTextWidget:SetAnchor(1.0, 1.0);
                v.lineRightTextWidget:SetRelativePoint(1.0, 1.0);
            end

            local rightTextWidth, rightTextHeight = UI.CalculateTextSize(v.lineRightText, v.lineRightTextTemplate)

            textWidth += 5 + rightTextWidth
            textHeight = math.max(textHeight, rightTextHeight)
        end

        tooltipCurrentWidth = math.max(tooltipCurrentWidth, textWidth)
        tooltipCurrentHeight += textHeight + lineHeightPadding
    end

    tooltipCurrentWidth += 20
    tooltipCurrentHeight += 5
    tooltipPanel:SetSize(tooltipCurrentWidth, tooltipCurrentHeight)
    
    tooltipPanelData.sizeX = tooltipCurrentWidth
    tooltipPanelData.sizeY = tooltipCurrentHeight
end

local function AddLine(text : string, textTemplate : string)
    local line : TooltipLine = { lineLeftText = text, lineRightText = nil, lineLeftTextTemplate = textTemplate, lineRightTextTemplate = nil, lineLeftTextWidget = nil, lineRightTextWidget = nil }
    table.insert(tooltipLines, line)
end

local function AddDoubleLine(leftText : string, rightText : string, leftTextTemplate : string, rightTextTemplate : string)
    local line : TooltipLine = { lineLeftText = leftText, lineRightText = rightText, lineLeftTextTemplate = leftTextTemplate, lineRightTextTemplate = rightTextTemplate, lineLeftTextWidget = nil, lineRightTextWidget = nil }
    table.insert(tooltipLines, line)
end

local function ClearLines()
   table.clear(tooltipLines);
end

local function CreateGameTooltip()
    local canvas = UI.GetCanvas("GameTooltip", 0, 0, 1920, 1080); -- TODO: We don't want to hardcode the reference resolution here

    tooltipPanel = canvas:NewPanel(845, 476, 230, 108, 0, "GameTooltipBackgroundPanel");
    tooltipPanel:SetEnabled(true);
    tooltipPanel:SetAnchor(0.0, 0.0);
    tooltipPanel:SetRelativePoint(0.0, 0.0);

    tooltipPanelData.startMousePosX = 0
    tooltipPanelData.startMousePosY = 0
    tooltipPanelData.startPanelPosX = 0
    tooltipPanelData.startPanelPosY = 0
    tooltipPanelData.sizeX = 230
    tooltipPanelData.sizeY = 108

    tooltipPanel:SetOnMouseDown(function(eventID, widget, mouseX, mouseY)
        tooltipPanelData.startMousePosX = mouseX
        tooltipPanelData.startMousePosY = mouseY

        tooltipPanelData.startPanelPosX = tooltipPanel:GetWorldPosX()
        tooltipPanelData.startPanelPosY = tooltipPanel:GetWorldPosY()
    end);
    
    tooltipPanel:SetOnMouseHeld(function(eventID, widget, mouseX, mouseY)
        local mouseDeltaX = tooltipPanelData.startMousePosX - mouseX
        local mouseDeltaY = tooltipPanelData.startMousePosY - mouseY

        local posX = (tooltipPanelData.startPanelPosX - mouseDeltaX)
        local posY = (tooltipPanelData.startPanelPosY - mouseDeltaY)

        posX = math.clamp(posX, 0, 1920 - tooltipPanelData.sizeX)
        posY = math.clamp(posY, 0, 1080 - tooltipPanelData.sizeY)

        tooltipPanel:SetWorldPos(posX, posY);
    end);

    -- Dummy Table Mirroring Item.cdb
    local itemTemplate = Item.GetItemInfo(1)

    if itemTemplate == nil then
        itemTemplate = {
            ID = 1,
            DisplayID = 36377,
            Flags = 0,
            Rarity = ItemUtil.Rarity.Unique,
            Category = ItemUtil.Category.Weapon,
            Type = ItemUtil.CategoryType.Weapon.Sword_1H,
            VirtualLevel = 9999,
            RequiredLevel = 1,
            Name = "Sword of a Thousand Truths",
            Description = "",
            RequiredText = "",
            SpecialText = "Equip: Gain the one true perspective",
            CustomInt1 = 100,
            CustomInt2 = 9999,
            CustomInt3 = 10000,
            CustomInt4 = 0,
            CustomInt5 = 0,
            CustomInt6 = 0,
            CustomInt7 = 0,
            CustomInt8 = 0,
            CustomInt9 = 0,
            CustomInt10 = 0,
            CustomFloat1 = 1.60,
            CustomFloat2 = 0,
            CustomFloat3 = 0,
            CustomFloat4 = 0,
            CustomFloat5 = 0
        }

    end

    ClearLines()

    local itemNameFontTemplate = "GameTooltipLargeText-" .. ItemUtil.GetRarityText(itemTemplate.Rarity)

    local itemTypeName = ItemUtil.GetCategoryTypeText(itemTemplate.Category, itemTemplate.Type)
    local itemWeaponStyleText = ItemUtil.GetWeaponStyleTextFromWeaponCategoryType(itemTemplate.Type)
    local itemSpeed = if itemTemplate.CustomFloat1 > 0 then itemTemplate.CustomFloat1 else 1.0
    local itemSpeedText = "Speed " .. string.format("%.2f", tostring(itemTemplate.CustomFloat1))
    local itemDamageRangeText = tostring(itemTemplate.CustomInt2) .. " - " .. tostring(itemTemplate.CustomInt3) .. " Damage"
    local itemDps = ((itemTemplate.CustomInt2 + itemTemplate.CustomInt3) / 2.0) / itemSpeed
    local itemDpsText = "(" .. string.format("%.2f", tostring(itemDps)) .. " damage per second)"
    
    local name = if itemTemplate.Name:len() > 0 then itemTemplate.Name else "Unknown"
    AddLine(name,              itemNameFontTemplate)
    AddDoubleLine(itemWeaponStyleText,      itemTypeName,           "GameTooltipMediumText", "GameTooltipMediumText")
    AddDoubleLine(itemDamageRangeText,      itemSpeedText,          "GameTooltipMediumText", "GameTooltipMediumText")
    AddLine(itemDpsText,                                            "GameTooltipMediumText")
    
    if (itemTemplate.CustomInt1 > 0) then
        local itemDurabilityText = "Durability " .. tostring(itemTemplate.CustomInt1) .. " / " .. tostring(itemTemplate.CustomInt1)
        AddLine(itemDurabilityText,                                     "GameTooltipMediumText")
    end

    if itemTemplate.RequiredLevel > 0 then
        local itemRequiredLevelText = "Requires Level " .. tostring(itemTemplate.RequiredLevel)
        AddLine(itemRequiredLevelText,                                  "GameTooltipMediumText")
    end

    if itemTemplate.RequiredText:len() > 0 then
        AddLine(itemTemplate.RequiredText,                                  "GameTooltipMediumText")
    end

    if itemTemplate.SpecialText:len() > 0 then
        AddLine(itemTemplate.SpecialText,                                  "GameTooltipMediumText-Green")
    end
    
    if itemTemplate.Description:len() > 0 then
        AddLine(itemTemplate.Description,                                  "GameTooltipMediumText-Yellow")
    end

    RefreshGameTooltip()
end

local function OnGameLoaded(eventID : number, motd : string)
    RegisterTemplates();

    CreateGameTooltip();
end

RegisterGameEvent(GameEvent.Loaded, OnGameLoaded)