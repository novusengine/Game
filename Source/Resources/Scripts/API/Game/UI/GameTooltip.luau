export type GameTooltipAPI =
{
    Initialize : (self : GameTooltipAPI) -> (),
    Refresh : () -> (),
    SetItem : (itemID : integer) -> (),
    SetSpell : (spellID : integer) -> (),
    AddLine : (text : string, textTemplate : string, textColor : vec?, textHeightPadding : number?) -> (),
    AddDoubleLine : (leftText : string, rightText : string, leftTextTemplate : string, rightTextTemplate : string, leftTextColor : vec?, rightTextColor : vec?, textHeightPadding : number?) -> (),
    ClearLines : () -> (),
    Show : () -> (),
    Hide : () -> (),
    Toggle : () -> (),
    IsShown : () -> boolean,
    ContainsItem : (itemID : integer) -> boolean,
}

local gameTooltipAPI : GameTooltipAPI = { } :: any

local ItemUtil = require("@src/API/Game/ItemUtil")

local tooltipMinWidth = 150

local function RegisterTemplates()
    local textWrapWidth = 261.1

    UI.RegisterTextTemplate("GameTooltipLargeText",
    {
        font = "Data/Fonts/OpenSans-Regular.ttf",
        size = 12.0,
        color = vec.New(255.0/255.0, 255.0/255.0, 255.0/255.0),
        borderSize = 0.5,
        borderColor = vec.New(0.05, 0.05, 0.05),
        wrapWidth = textWrapWidth
    });

    UI.RegisterTextTemplate("GameTooltipMediumText",
    {
        font = "Data/Fonts/OpenSans-Regular.ttf",
        size = 10.5,
        color = vec.New(255.0/255.0, 255.0/255.0, 255.0/255.0),
        borderSize = 0.5,
        borderColor = vec.New(0.05, 0.05, 0.05),
        wrapWidth = textWrapWidth
    });

    UI.RegisterTextTemplate("GameTooltipDebugText",
    {
        font = "Data/Fonts/OpenSans-Regular.ttf",
        size = 11.0,
        color = vec.New(255.0/255.0, 255.0/255.0, 255.0/255.0),
        borderSize = 0.5,
        borderColor = vec.New(0.05, 0.05, 0.05),
        wrapWidth = textWrapWidth
    });
    
    UI.RegisterPanelTemplate("GameTooltipBackgroundPanel",
    {
        color = vec.New(11.5/255.0, 22.25/255.0, 33.0/255.0),
        alpha = 0.98,
        cornerRadius = 3.0,
        borderColor = vec.New(0.20, 0.28, 0.38),
        borderSize = 1.0,
    });
end

type Point2D = { x : number, y : number }
type TooltipLine = { lineLeftText : string, lineRightText : string?, lineLeftTextTemplate : string, lineRightTextTemplate : string?, lineLeftTextColor : vec?, lineRightTextColor : vec?, lineLeftTextWidget : TextWidget?, lineRightTextWidget : TextWidget?, lineTextHeightPadding : number }

local state = {
    tooltipPanel = nil,
    tooltipPanelData = { },
    tooltipLines = { },
    whiteColor = vec.New(255.0/255.0, 255.0/255.0, 255.0/255.0),
    yellowColor = vec.New(255.0/255.0, 210.0/255.0, 0.0/255.0),
} :: any
local initialized = false

function gameTooltipAPI:Initialize()
    if initialized then return end
    initialized = true
    RegisterTemplates()

    local canvas = UI.GetCanvas("GameTooltip", vec.New(0, 0), vec.New(1920, 1080)); -- TODO: We don't want to hardcode the reference resolution here
    state.tooltipPanel = canvas:NewPanel(vec.New(1200, 250), vec.New(tooltipMinWidth, 108), 9999i, "GameTooltipBackgroundPanel");
    state.tooltipPanel:SetEnabled(false)
    state.tooltipPanel:SetAnchor(vec.New(0.0, 0.0));
    state.tooltipPanel:SetRelativePoint(vec.New(0.0, 0.0));
    state.tooltipPanelData.dragOffsetX = 0
    state.tooltipPanelData.dragOffsetY = 0
    state.tooltipPanelData.isEnabled = false
    state.tooltipPanelData.itemID = 0i
    state.tooltipPanelData.spellID = 0i

    state.tooltipPanel:SetOnMouseDown(function(eventID, widget, button, mouseX, mouseY)
        state.tooltipPanelData.dragOffsetX = mouseX - widget:GetWorldPosX()
        state.tooltipPanelData.dragOffsetY = mouseY - widget:GetWorldPosY()
    end);

    state.tooltipPanel:SetOnMouseHeld(function(eventID, widget, mouseX, mouseY)
        local offsetX = state.tooltipPanelData.dragOffsetX or 0
        local offsetY = state.tooltipPanelData.dragOffsetY or 0
        local posX = math.clamp(mouseX - offsetX, 0, 1920 - widget:GetWidth())
        local posY = math.clamp(mouseY - offsetY, 0, 1080 - widget:GetHeight())
        widget:SetWorldPos(vec.New(posX, posY))
    end)
end

gameTooltipAPI.Refresh = function()
    local tooltipCurrentWidth, tooltipCurrentHeight = 0, 3

    for k, v in state.tooltipLines do
        local linePosY = tooltipCurrentHeight
        local isDoubleLine = v.lineRightText ~= nil

        if v.lineLeftTextWidget == nil and v.lineLeftText:len() > 0  then
            v.lineLeftTextWidget = state.tooltipPanel:NewText(v.lineLeftText, vec.New(5, -linePosY), 0i, v.lineLeftTextTemplate);
            v.lineLeftTextWidget:SetEnabled(true)
            v.lineLeftTextWidget:SetAnchor(vec.New(0.0, 1.0));
            v.lineLeftTextWidget:SetRelativePoint(vec.New(0.0, 1.0));

            if v.lineLeftTextColor then
                v.lineLeftTextWidget:SetColor(v.lineLeftTextColor)
            end
        end

        local textSize = v.lineLeftTextWidget:GetSize()
        local textWidth, textHeight = textSize.x, textSize.y

        if isDoubleLine then
            if v.lineRightTextWidget == nil and v.lineRightText:len() > 0 then 
                v.lineRightTextWidget = state.tooltipPanel:NewText(v.lineRightText, vec.New(-5, -linePosY), 0i, v.lineRightTextTemplate);
                v.lineRightTextWidget:SetEnabled(true)
                v.lineRightTextWidget:SetAnchor(vec.New(1.0, 1.0));
                v.lineRightTextWidget:SetRelativePoint(vec.New(1.0, 1.0));
                
                if v.lineRightTextColor then
                    v.lineRightTextWidget:SetColor(v.lineRightTextColor)
                end
            end

            local rightTextSize = v.lineRightTextWidget:GetSize()
            local rightTextWidth, rightTextHeight = rightTextSize.x, rightTextSize.y

            textWidth += 5 + rightTextWidth
            textHeight = math.max(textHeight, rightTextHeight)
        end

        tooltipCurrentWidth = math.max(tooltipCurrentWidth, textWidth)
        tooltipCurrentHeight += textHeight + v.lineTextHeightPadding
    end

    tooltipCurrentWidth = math.max(tooltipMinWidth, tooltipCurrentWidth + 10)
    tooltipCurrentHeight += 5

    state.tooltipPanel:SetSize(vec.New(tooltipCurrentWidth, tooltipCurrentHeight))
end

gameTooltipAPI.SetItem = function(itemID)
    if itemID == 0i then
        return
    end

    if state.tooltipPanelData.itemID == itemID then
        return
    end

    local itemInfo : ItemInfo = Item.GetItemInfo(itemID)
    if itemInfo == nil then
        return
    end

    gameTooltipAPI.ClearLines()
    state.tooltipPanelData.itemID = itemID
    state.tooltipPanelData.spellID = 0i

    local itemArmorTemplate     : ItemArmorInfo  | nil = nil
    local itemWeaponTemplate    : ItemWeaponInfo | nil = nil
    local itemShieldTemplate    : ItemShieldInfo | nil = nil
    
    if itemInfo.ArmorTemplateID ~= 0i then
        itemArmorTemplate = Item.GetItemArmorInfo(itemInfo.ArmorTemplateID)
    elseif itemInfo.WeaponTemplateID ~= 0i then
        itemWeaponTemplate = Item.GetItemWeaponInfo(itemInfo.WeaponTemplateID)
    elseif itemInfo.ShieldTemplateID ~= 0i then
        itemShieldTemplate = Item.GetItemShieldInfo(itemInfo.ShieldTemplateID)
    end
    
    local itemRarityColor = ItemUtil.GetRarityColor(itemInfo.Rarity)
    local itemTypeName = ItemUtil.GetCategoryTypeText(itemInfo.Category, itemInfo.Type)
    local name = if itemInfo.Name:len() > 0 then itemInfo.Name else "Unknown"
    local itemBonusArmor = 0i
    local itemBlock = 0i
    gameTooltipAPI.AddLine(name, "GameTooltipLargeText", itemRarityColor)

    if itemInfo.VirtualLevel ~= 0i then
        local itemVirtualLevelText = "Item Level " .. tostring(itemInfo.VirtualLevel)
        gameTooltipAPI.AddLine(itemVirtualLevelText, "GameTooltipMediumText", state.yellowColor)
    end

    if itemInfo.Bind ~= 0i then
        local itemBindText = ItemUtil.GetBindTypeText(itemInfo.Bind)
        gameTooltipAPI.AddLine(itemBindText, "GameTooltipMediumText")
    end

    if itemInfo.Category == ItemUtil.Category.Container then
        if itemInfo.Durability ~= 0i then
            local itemDurabilityText = tostring(itemInfo.Durability) .. " Slot Bag"
            gameTooltipAPI.AddLine(itemDurabilityText, "GameTooltipMediumText")
        end
    else
        if itemArmorTemplate ~= nil then
            local equipTypeText = ItemUtil.GetEquipTypeText(itemArmorTemplate.EquipType)
        
            if itemInfo.Category == ItemUtil.Category.Armor
                and integer.gt(itemInfo.Type, ItemUtil.CategoryType.Armor.Armor)
            then
                local itemCategoryType = ItemUtil.GetCategoryTypeText(itemInfo.Category, itemInfo.Type)
                gameTooltipAPI.AddDoubleLine(equipTypeText, itemCategoryType, "GameTooltipMediumText", "GameTooltipMediumText")
            else
                gameTooltipAPI.AddLine(equipTypeText, "GameTooltipMediumText")
            end

            itemBonusArmor = itemArmorTemplate.BonusArmor
        elseif itemWeaponTemplate ~= nil then
            local itemWeaponStyleText = ItemUtil.GetWeaponStyleTextFromWeaponCategoryType(itemInfo.Type)
            local itemSpeed = if itemWeaponTemplate.Speed > 0 then itemWeaponTemplate.Speed else 1.0
            local itemSpeedText = "Speed " .. string.format("%.2f", tostring(itemWeaponTemplate.Speed))
            local itemDamageRangeText = tostring(itemWeaponTemplate.MinDamage) .. " - " .. tostring(itemWeaponTemplate.MaxDamage) .. " Damage"
            local minimumDamage = integer.tonumber(itemWeaponTemplate.MinDamage)
            local maximumDamage = integer.tonumber(itemWeaponTemplate.MaxDamage)
            local itemDps = ((minimumDamage + maximumDamage) / 2.0) / itemSpeed
            local itemDpsText = "(" .. string.format("%.2f", tostring(itemDps)) .. " damage per second)"

            gameTooltipAPI.AddDoubleLine(itemWeaponStyleText,      itemTypeName,    "GameTooltipMediumText", "GameTooltipMediumText", state.whiteColor, state.whiteColor, 1)
            gameTooltipAPI.AddDoubleLine(itemDamageRangeText,      itemSpeedText,   "GameTooltipMediumText", "GameTooltipMediumText", state.whiteColor, state.whiteColor, 0)
            gameTooltipAPI.AddLine(itemDpsText,                                     "GameTooltipMediumText")

        elseif itemShieldTemplate ~= nil then
            itemBonusArmor = itemShieldTemplate.BonusArmor
            itemBlock = itemShieldTemplate.Block

            gameTooltipAPI.AddDoubleLine("Off Hand",      itemTypeName, "GameTooltipMediumText", "GameTooltipMediumText")
        end

        local itemArmor = integer.add(itemInfo.Armor, itemBonusArmor)
        if integer.gt(itemArmor, 0i) then
            local hasBonusArmor = integer.gt(itemBonusArmor, 0i)
            local itemArmorText = tostring(itemArmor) .. " Armor"
            local armorTextColor = if hasBonusArmor then vec.New(30.0/255.0, 255.0/255.0, 0.0/255.0) else nil

            if integer.gt(itemArmor, 0i) then
                gameTooltipAPI.AddLine(itemArmorText, "GameTooltipMediumText", armorTextColor)
            end
        end

        if integer.gt(itemBlock, 0i) then
            local shieldBlockText = tostring(itemBlock) .. " Block"
            gameTooltipAPI.AddLine(shieldBlockText, "GameTooltipMediumText")
        end

        local itemStatTemplateID = itemInfo.StatTemplateID
        if itemStatTemplateID ~= 0i then
            local itemStats : ItemStatInfo = Item.GetItemStatInfo(itemStatTemplateID)
            local numStats = #itemStats

            for i = 1, numStats do
                local stat : ItemStat = itemStats[i]
                                
                local prefix = if integer.gt(stat.Value, 0i) then "+" else ""
                local itemStatText = prefix .. tostring(stat.Value) .. " " .. ItemUtil.GetStatText(stat.ID)
                gameTooltipAPI.AddLine(itemStatText, "GameTooltipMediumText")
            end
        end

        if itemInfo.Durability ~= 0i then
            local itemDurabilityText = "Durability " .. tostring(itemInfo.Durability) .. " / " .. tostring(itemInfo.Durability)
            gameTooltipAPI.AddLine(itemDurabilityText, "GameTooltipMediumText")
        end

        if itemInfo.RequiredLevel ~= 0i then
            local itemRequiredLevelText = "Requires Level " .. tostring(itemInfo.RequiredLevel)
            gameTooltipAPI.AddLine(itemRequiredLevelText, "GameTooltipMediumText")
        end
        
        local itemEffects = Item.GetItemEffects(itemID)
        local itemEffectTextColor = vec.New(30.0/255.0, 255.0/255.0, 0.0/255.0)
        for _, v in itemEffects do
            if integer.gt(v.Type, ItemEffectType.OnProc) then
                continue
            end
    
            local spellInfo = Spell.GetSpellInfo(v.SpellID)
            
            if spellInfo.Description:len() > 0 then
                local typePrefix = ItemUtil.GetItemEffectTypePrefix(integer.add(v.Type, 1i))
                local effectText = typePrefix .. spellInfo.Description
                gameTooltipAPI.AddLine(effectText, "GameTooltipMediumText", itemEffectTextColor, 1)
            end
        end
    end
    
    if itemInfo.Description:len() > 0 then
        gameTooltipAPI.AddLine(itemInfo.Description, "GameTooltipMediumText", state.yellowColor)
    end

    gameTooltipAPI.AddDoubleLine("ItemID", tostring(itemID), "GameTooltipDebugText", "GameTooltipDebugText", state.yellowColor, state.whiteColor, 2)
    gameTooltipAPI.AddDoubleLine("IconID", tostring(itemInfo.IconID), "GameTooltipDebugText", "GameTooltipDebugText", state.yellowColor, state.whiteColor, 2)
    gameTooltipAPI.AddDoubleLine("DisplayID", tostring(itemInfo.DisplayID), "GameTooltipDebugText", "GameTooltipDebugText", state.yellowColor, state.whiteColor)
    gameTooltipAPI.Refresh()
end

gameTooltipAPI.SetSpell = function(spellID : integer)
    if spellID == 0i or state.tooltipPanelData.spellID == spellID then return end
    local spellInfo : SpellInfo = Spell.GetSpellInfo(spellID)
    if spellInfo == nil then return end

    gameTooltipAPI.ClearLines()
    state.tooltipPanelData.itemID = 0i
    state.tooltipPanelData.spellID = spellID
    gameTooltipAPI.AddLine(spellInfo.Name, "GameTooltipLargeText", state.whiteColor)
    if spellInfo.Description:len() > 0 then
        gameTooltipAPI.AddLine(spellInfo.Description, "GameTooltipMediumText", state.yellowColor)
    end
    gameTooltipAPI.AddDoubleLine("SpellID", tostring(spellID), "GameTooltipDebugText", "GameTooltipDebugText", state.yellowColor, state.whiteColor)
    gameTooltipAPI.Refresh()
end

gameTooltipAPI.AddLine = function(text : string, textTemplate : string, textColor : vec?, textHeightPadding : number?)
    local heightPadding = if textHeightPadding then textHeightPadding else 3
    local line : TooltipLine = { lineLeftText = text, lineRightText = nil, lineLeftTextTemplate = textTemplate, lineRightTextTemplate = nil, lineLeftTextColor = textColor, lineRightTextColor = nil, lineLeftTextWidget = nil, lineRightTextWidget = nil, lineTextHeightPadding = heightPadding }
    table.insert(state.tooltipLines, line)
end

gameTooltipAPI.AddDoubleLine = function(leftText : string, rightText : string, leftTextTemplate : string, rightTextTemplate : string, leftTextColor : vec?, rightTextColor : vec?, textHeightPadding : number?)
    local heightPadding = if textHeightPadding then textHeightPadding else 3
    local line : TooltipLine = { lineLeftText = leftText, lineRightText = rightText, lineLeftTextTemplate = leftTextTemplate, lineRightTextTemplate = rightTextTemplate, lineLeftTextColor = leftTextColor, lineRightTextColor = rightTextColor, lineLeftTextWidget = nil, lineRightTextWidget = nil, lineTextHeightPadding = heightPadding }
    table.insert(state.tooltipLines, line)
end

gameTooltipAPI.ClearLines = function()
    local numTooltipLines = #state.tooltipLines
    if numTooltipLines > 0 then
        for k, v in state.tooltipLines do
            if v.lineLeftTextWidget then
                UI.DestroyWidget(v.lineLeftTextWidget)
            end

            if v.lineRightTextWidget then
                UI.DestroyWidget(v.lineRightTextWidget)
            end
        end
        table.clear(state.tooltipLines);
    end
    state.tooltipPanelData.itemID = 0i
    state.tooltipPanelData.spellID = 0i
end

gameTooltipAPI.Show = function()
    state.tooltipPanel:SetEnabled(true)
    state.tooltipPanelData.isEnabled = true
end

gameTooltipAPI.Hide = function()
    state.tooltipPanel:SetEnabled(false)
    state.tooltipPanelData.isEnabled = false
end

gameTooltipAPI.Toggle = function()
    local isEnabled = state.tooltipPanelData.isEnabled

    if isEnabled then
        gameTooltipAPI.Hide()
    else
        gameTooltipAPI.Show()
    end
end

gameTooltipAPI.IsShown = function()
    return state.tooltipPanelData.isEnabled
end

gameTooltipAPI.ContainsItem = function(itemID : integer)
    return itemID == state.tooltipPanelData.itemID
end

return gameTooltipAPI
