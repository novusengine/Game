local gameTooltipAPI = { }

local ItemUtil = require("@src/API/Game/ItemUtil")

local function RegisterTemplates()
    local textWrapWidth = 261.1

    UI.RegisterTextTemplate("GameTooltipLargeText",
    {
        font = "Data/Fonts/OpenSans-Regular.ttf",
        size = 12.0,
        color = vector.create(255.0/255.0, 255.0/255.0, 255.0/255.0),
        borderSize = 0.5,
        borderColor = vector.create(0.05, 0.05, 0.05),
        wrapWidth = textWrapWidth
    });

    UI.RegisterTextTemplate("GameTooltipMediumText",
    {
        font = "Data/Fonts/OpenSans-Regular.ttf",
        size = 10.5,
        color = vector.create(255.0/255.0, 255.0/255.0, 255.0/255.0),
        borderSize = 0.5,
        borderColor = vector.create(0.05, 0.05, 0.05),
        wrapWidth = textWrapWidth
    });

    UI.RegisterTextTemplate("GameTooltipDebugText",
    {
        font = "Data/Fonts/OpenSans-Regular.ttf",
        size = 11.0,
        color = vector.create(255.0/255.0, 255.0/255.0, 255.0/255.0),
        borderSize = 0.5,
        borderColor = vector.create(0.05, 0.05, 0.05),
        wrapWidth = textWrapWidth
    });
    
    UI.RegisterPanelTemplate("GameTooltipBackgroundPanel",
    {
        background = "Data/Texture/interface/tooltips/ui-tooltip-background.dds",
        color = vector.create(11.5/255.0, 22.25/255.0, 33.0/255.0),
        cornerRadius = 3.0
    });
end

RegisterTemplates()

type Point2D = { x : number, y : number }
type TooltipLine = { lineLeftText : string, lineRightText : string, lineLeftTextTemplate : string, lineRightTextTemplate : string, lineLeftTextColor : vector, lineRightTextColor : vector, lineLeftTextWidget : TextWidget, lineRightTextWidget : TextWidget, lineTextHeightPadding : number }

gameTooltipAPI.tooltipPanel = nil
gameTooltipAPI.tooltipPanelData = { }
gameTooltipAPI.tooltipLines = { }

local canvas = UI.GetCanvas("gameTooltipAPI", 0, 0, 1920, 1080); -- TODO: We don't want to hardcode the reference resolution here

gameTooltipAPI.tooltipPanel = canvas:NewPanel(1200, 250, 150, 108, 9999, "GameTooltipBackgroundPanel");
gameTooltipAPI.tooltipPanel:SetEnabled(false)
gameTooltipAPI.tooltipPanel:SetAnchor(0.0, 0.0);
gameTooltipAPI.tooltipPanel:SetRelativePoint(0.0, 0.0);

gameTooltipAPI.tooltipPanelData.dragOffsetX = 0
gameTooltipAPI.tooltipPanelData.dragOffsetY = 0
gameTooltipAPI.tooltipPanelData.isEnabled = false
gameTooltipAPI.tooltipPanelData.itemID = 0
gameTooltipAPI.whiteColor = vector.create(255.0/255.0, 255.0/255.0, 255.0/255.0)
gameTooltipAPI.yellowColor = vector.create(255.0/255.0, 210.0/255.0, 0.0/255.0)

gameTooltipAPI.tooltipPanel:SetOnMouseDown(function(eventID, widget, button, mouseX, mouseY)
    gameTooltipAPI.tooltipPanelData.dragOffsetX = mouseX - widget:GetWorldPosX()
    gameTooltipAPI.tooltipPanelData.dragOffsetY = mouseY - widget:GetWorldPosY()
end);

gameTooltipAPI.tooltipPanel:SetOnMouseHeld(function(eventID, widget, mouseX, mouseY)
    local offsetX = gameTooltipAPI.tooltipPanelData.dragOffsetX or 0
    local offsetY = gameTooltipAPI.tooltipPanelData.dragOffsetY or 0

    local posX = math.clamp(mouseX - offsetX, 0, 1920 - widget:GetWidth())
    local posY = math.clamp(mouseY - offsetY, 0, 1080 - widget:GetHeight())

    widget:SetWorldPos(posX, posY)
end)

gameTooltipAPI.Refresh = function()
    local tooltipCurrentWidth, tooltipCurrentHeight = 0, 3

    for k, v in gameTooltipAPI.tooltipLines do
        local linePosY = tooltipCurrentHeight
        local isDoubleLine = v.lineRightText ~= nil

        if v.lineLeftTextWidget == nil and v.lineLeftText:len() > 0  then
            v.lineLeftTextWidget = gameTooltipAPI.tooltipPanel:NewText(v.lineLeftText, 5, -linePosY, 0, v.lineLeftTextTemplate);
            v.lineLeftTextWidget:SetEnabled(true)
            v.lineLeftTextWidget:SetAnchor(0.0, 1.0);
            v.lineLeftTextWidget:SetRelativePoint(0.0, 1.0);

            if v.lineLeftTextColor then
                v.lineLeftTextWidget:SetColor(v.lineLeftTextColor)
            end
        end

        local textWidth, textHeight = v.lineLeftTextWidget:GetSize()

        if isDoubleLine then
            if v.lineRightTextWidget == nil and v.lineRightText:len() > 0 then 
                v.lineRightTextWidget = gameTooltipAPI.tooltipPanel:NewText(v.lineRightText, -5, -linePosY, 0, v.lineRightTextTemplate);
                v.lineRightTextWidget:SetEnabled(true)
                v.lineRightTextWidget:SetAnchor(1.0, 1.0);
                v.lineRightTextWidget:SetRelativePoint(1.0, 1.0);
                
                if v.lineRightTextColor then
                    v.lineRightTextWidget:SetColor(v.lineRightTextColor)
                end
            end

            local rightTextWidth, rightTextHeight = v.lineRightTextWidget:GetSize()

            textWidth += 5 + rightTextWidth
            textHeight = math.max(textHeight, rightTextHeight)
        end

        tooltipCurrentWidth = math.max(tooltipCurrentWidth, textWidth)
        tooltipCurrentHeight += textHeight + v.lineTextHeightPadding
    end

    tooltipCurrentWidth += 10
    tooltipCurrentHeight += 5

    gameTooltipAPI.tooltipPanel:SetSize(tooltipCurrentWidth, tooltipCurrentHeight)
end

gameTooltipAPI.SetItem = function(itemID)
    if itemID == 0 then
        return
    end

    if gameTooltipAPI.tooltipPanelData.itemID == itemID then
        return
    end

    local itemInfo : ItemInfo = Item.GetItemInfo(itemID)
    if itemInfo == nil then
        return
    end

    gameTooltipAPI.ClearLines()
    gameTooltipAPI.tooltipPanelData.itemID = itemID

    local itemArmorTemplate     : ItemArmorInfo  | nil = nil
    local itemWeaponTemplate    : ItemWeaponInfo | nil = nil
    local itemShieldTemplate    : ItemShieldInfo | nil = nil
    
    if itemInfo.ArmorTemplateID > 0 then
        itemArmorTemplate = Item.GetItemArmorInfo(itemInfo.ArmorTemplateID)
    elseif itemInfo.WeaponTemplateID > 0 then
        itemWeaponTemplate = Item.GetItemWeaponInfo(itemInfo.WeaponTemplateID)
    elseif itemInfo.ShieldTemplateID > 0 then
        itemShieldTemplate = Item.GetItemShieldInfo(itemInfo.ShieldTemplateID)
    end
    
    local itemRarityColor = ItemUtil.GetRarityColor(itemInfo.Rarity)
    local itemTypeName = ItemUtil.GetCategoryTypeText(itemInfo.Category, itemInfo.Type)
    local name = if itemInfo.Name:len() > 0 then itemInfo.Name else "Unknown"
    local itemBonusArmor = 0
    local itemBlock = 0
    gameTooltipAPI.AddLine(name, "GameTooltipLargeText", itemRarityColor)

    if itemInfo.VirtualLevel > 0 then
        local itemVirtualLevelText = "Item Level " .. tostring(itemInfo.VirtualLevel)
        gameTooltipAPI.AddLine(itemVirtualLevelText, "GameTooltipMediumText", gameTooltipAPI.yellowColor)
    end

    if (itemInfo.Bind > 0) then
        local itemBindText = ItemUtil.GetBindTypeText(itemInfo.Bind)
        gameTooltipAPI.AddLine(itemBindText, "GameTooltipMediumText")
    end

    if itemInfo.Category == ItemUtil.Category.Container then
        if itemInfo.Durability > 0 then
            local itemDurabilityText = tostring(itemInfo.Durability) .. " Slot Bag"
            gameTooltipAPI.AddLine(itemDurabilityText, "GameTooltipMediumText")
        end
    else
        if itemArmorTemplate ~= nil then
            local equipTypeText = ItemUtil.GetEquipTypeText(itemArmorTemplate.EquipType)
        
            if itemInfo.Category == ItemUtil.Category.Armor and itemInfo.Type > ItemUtil.CategoryType.Armor.Armor then
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
            local itemDps = ((itemWeaponTemplate.MinDamage + itemWeaponTemplate.MaxDamage) / 2.0) / itemSpeed
            local itemDpsText = "(" .. string.format("%.2f", tostring(itemDps)) .. " damage per second)"

            gameTooltipAPI.AddDoubleLine(itemWeaponStyleText,      itemTypeName,    "GameTooltipMediumText", "GameTooltipMediumText", gameTooltipAPI.whiteColor, gameTooltipAPI.whiteColor, 1)
            gameTooltipAPI.AddDoubleLine(itemDamageRangeText,      itemSpeedText,   "GameTooltipMediumText", "GameTooltipMediumText", gameTooltipAPI.whiteColor, gameTooltipAPI.whiteColor, 0)
            gameTooltipAPI.AddLine(itemDpsText,                                     "GameTooltipMediumText")

        elseif itemShieldTemplate ~= nil then
            itemBonusArmor = itemShieldTemplate.BonusArmor
            itemBlock = itemShieldTemplate.Block

            gameTooltipAPI.AddDoubleLine("Off Hand",      itemTypeName, "GameTooltipMediumText", "GameTooltipMediumText")
        end

        local itemArmor = if itemInfo.Armor then itemInfo.Armor + itemBonusArmor else 0
        if itemArmor > 0 then
            local hasBonusArmor = itemBonusArmor > 0
            local itemArmorText = tostring(itemArmor) .. " Armor"
            local armorTextColor = if hasBonusArmor then vector.create(30.0/255.0, 255.0/255.0, 0.0/255.0) else nil

            if itemArmor > 0 then
                gameTooltipAPI.AddLine(itemArmorText, "GameTooltipMediumText", armorTextColor)
            end
        end

        if itemBlock > 0 then
            local shieldBlockText = tostring(itemBlock) .. " Block"
            gameTooltipAPI.AddLine(shieldBlockText, "GameTooltipMediumText")
        end

        local itemStatTemplateID = if itemInfo.StatTemplateID then itemInfo.StatTemplateID else 0
        if itemStatTemplateID > 0 then
            local itemStats : ItemStatInfo = Item.GetItemStatInfo(itemStatTemplateID)
            local numStats = #itemStats

            for i = 1, numStats do
                local stat : ItemStat = itemStats[i]
                                
                local prefix = if stat.Value > 0 then "+" else ""
                local itemStatText = prefix .. tostring(stat.Value) .. " " .. ItemUtil.GetStatText(stat.ID)
                gameTooltipAPI.AddLine(itemStatText, "GameTooltipMediumText")
            end
        end

        if itemInfo.Durability > 0 then
            local itemDurabilityText = "Durability " .. tostring(itemInfo.Durability) .. " / " .. tostring(itemInfo.Durability)
            gameTooltipAPI.AddLine(itemDurabilityText, "GameTooltipMediumText")
        end

        if itemInfo.RequiredLevel > 0 then
            local itemRequiredLevelText = "Requires Level " .. tostring(itemInfo.RequiredLevel)
            gameTooltipAPI.AddLine(itemRequiredLevelText, "GameTooltipMediumText")
        end
        
        local itemEffects = Item.GetItemEffects(itemID)
        local itemEffectTextColor = vector.create(30.0/255.0, 255.0/255.0, 0.0/255.0)
        for _, v in itemEffects do
            if v.Type > ItemEffectType.OnProc then
                continue
            end
    
            local spellInfo = Spell.GetSpellInfo(v.SpellID)
            
            if spellInfo.Description:len() > 0 then
                local typePrefix = ItemUtil.GetItemEffectTypePrefix(v.Type + 1)
                local effectText = typePrefix .. spellInfo.Description
                gameTooltipAPI.AddLine(effectText, "GameTooltipMediumText", itemEffectTextColor, 1)
            end
        end
    end
    
    if itemInfo.Description:len() > 0 then
        gameTooltipAPI.AddLine(itemInfo.Description, "GameTooltipMediumText", gameTooltipAPI.yellowColor)
    end

    gameTooltipAPI.AddDoubleLine("ItemID", tostring(itemID), "GameTooltipDebugText", "GameTooltipDebugText", gameTooltipAPI.yellowColor, gameTooltipAPI.whiteColor, 2)
    gameTooltipAPI.AddDoubleLine("IconID", tostring(itemInfo.IconID), "GameTooltipDebugText", "GameTooltipDebugText", gameTooltipAPI.yellowColor, gameTooltipAPI.whiteColor, 2)
    gameTooltipAPI.AddDoubleLine("DisplayID", tostring(itemInfo.DisplayID), "GameTooltipDebugText", "GameTooltipDebugText", gameTooltipAPI.yellowColor, gameTooltipAPI.whiteColor)
    gameTooltipAPI.Refresh()
end

gameTooltipAPI.AddLine = function(text : string, textTemplate : string, textColor : vector, textHeightPadding : number)
    local heightPadding = if textHeightPadding then textHeightPadding else 3
    local line : TooltipLine = { lineLeftText = text, lineRightText = nil, lineLeftTextTemplate = textTemplate, lineRightTextTemplate = nil, lineLeftTextColor = textColor, lineRightTextColor = nil, lineLeftTextWidget = nil, lineRightTextWidget = nil, lineTextHeightPadding = heightPadding }
    table.insert(gameTooltipAPI.tooltipLines, line)
end

gameTooltipAPI.AddDoubleLine = function(leftText : string, rightText : string, leftTextTemplate : string, rightTextTemplate : string, leftTextColor : vector, rightTextColor : vector, textHeightPadding : number)  
    local heightPadding = if textHeightPadding then textHeightPadding else 3
    local line : TooltipLine = { lineLeftText = leftText, lineRightText = rightText, lineLeftTextTemplate = leftTextTemplate, lineRightTextTemplate = rightTextTemplate, lineLeftTextColor = leftTextColor, lineRightTextColor = rightTextColor, lineLeftTextWidget = nil, lineRightTextWidget = nil, lineTextHeightPadding = heightPadding }
    table.insert(gameTooltipAPI.tooltipLines, line)
end

gameTooltipAPI.ClearLines = function()
    local numTooltipLines = #gameTooltipAPI.tooltipLines
    local itemID = gameTooltipAPI.tooltipPanelData.itemID

    if numTooltipLines == 0 or itemID == 0 then
        return
    end

    for k, v in gameTooltipAPI.tooltipLines do
        if v.lineLeftTextWidget then
            UI.DestroyWidget(v.lineLeftTextWidget)
        end

        if v.lineRightTextWidget then
            UI.DestroyWidget(v.lineRightTextWidget)
        end
    end

    table.clear(gameTooltipAPI.tooltipLines);
    gameTooltipAPI.tooltipPanelData.itemID = 0
end

gameTooltipAPI.Show = function()
    gameTooltipAPI.tooltipPanel:SetEnabled(true)
    gameTooltipAPI.tooltipPanelData.isEnabled = true
end

gameTooltipAPI.Hide = function()
    gameTooltipAPI.tooltipPanel:SetEnabled(false)
    gameTooltipAPI.tooltipPanelData.isEnabled = false
end

gameTooltipAPI.Toggle = function()
    local isEnabled = gameTooltipAPI.tooltipPanelData.isEnabled

    if isEnabled then
        gameTooltipAPI.Hide()
    else
        gameTooltipAPI.Show()
    end
end

gameTooltipAPI.IsShown = function()
    return gameTooltipAPI.tooltipPanelData.isEnabled
end

gameTooltipAPI.ContainsItem = function(itemID : number)
    return itemID == gameTooltipAPI.tooltipPanelData.itemID
end

return gameTooltipAPI