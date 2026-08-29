export type GameTooltip =
{
    Initialize : (self : GameTooltip) -> (),
    Refresh : () -> (),
    SetItem : (itemID : integer) -> (),
    SetSpell : (spellID : integer) -> (),
    AddLine : (text : string, textTemplate : string, textColor : vec?, textHeightPadding : number?) -> (),
    AddDoubleLine : (leftText : string, rightText : string, leftTextTemplate : string, rightTextTemplate : string, leftTextColor : vec?, rightTextColor : vec?, textHeightPadding : number?) -> (),
    ClearLines : () -> (),
    SetPosition : (position : vec) -> (),
    SetPositionAroundCursor : (cursorPosition : vec) -> (),
    Show : () -> (),
    Hide : () -> (),
    Toggle : () -> (),
    IsShown : () -> boolean,
    ContainsItem : (itemID : integer) -> boolean,
}
export type GameTooltipAPI = GameTooltip &
{
    New : (parent : (Canvas | Widget)) -> GameTooltip,
}

local ItemUtil = require("@src/API/Game/ItemUtil");

local tooltipMinWidth = 200;

local function RegisterTemplates()
    local textWrapWidth = 261.1;

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
RegisterTemplates();

type Point2D = { x : number, y : number }
type TooltipLine = { lineLeftText : string, lineRightText : string?, lineLeftTextTemplate : string, lineRightTextTemplate : string?, lineLeftTextColor : vec?, lineRightTextColor : vec?, lineLeftTextWidget : TextWidget?, lineRightTextWidget : TextWidget?, lineTextHeightPadding : number }

local function CreateTooltip(parent : (Canvas | Widget | string)) : GameTooltip
    local api : GameTooltip = { } :: any;
    local state = {
        tooltipPanel = nil,
        tooltipPanelData = { },
        tooltipLines = { },
        whiteColor = vec.New(255.0/255.0, 255.0/255.0, 255.0/255.0),
        yellowColor = vec.New(255.0/255.0, 210.0/255.0, 0.0/255.0),
    } :: any;

    function api:Initialize()
        local uiParent : any = parent;
        if type(parent) == "string" then
            uiParent = UI.GetCanvas(parent, vec.New(0, 0), vec.New(1920, 1080)); -- TODO: We don't want to hardcode the reference resolution here
        end
        state.tooltipPanel = uiParent:NewPanel(vec.New(1200, 250), vec.New(tooltipMinWidth, 108), 9999i, "GameTooltipBackgroundPanel");
        state.tooltipPanel:SetEnabled(false);
        state.tooltipPanel:SetAnchor(vec.New(0.0, 0.0));
        state.tooltipPanel:SetRelativePoint(vec.New(0.0, 0.0));
        state.tooltipPanelData.dragOffsetX = 0;
        state.tooltipPanelData.dragOffsetY = 0;
        state.tooltipPanelData.isEnabled = false;
        state.tooltipPanelData.itemID = 0i;
        state.tooltipPanelData.spellID = 0i;

        state.tooltipPanel:SetOnMouseDown(function(eventID, widget, button, mouseX, mouseY)
            state.tooltipPanelData.dragOffsetX = mouseX - widget:GetWorldPosX();
            state.tooltipPanelData.dragOffsetY = mouseY - widget:GetWorldPosY();
        end);

        state.tooltipPanel:SetOnMouseHeld(function(eventID, widget, mouseX, mouseY)
            local offsetX = state.tooltipPanelData.dragOffsetX or 0;
            local offsetY = state.tooltipPanelData.dragOffsetY or 0;
            local posX = math.clamp(mouseX - offsetX, 0, 1920 - widget:GetWidth());
            local posY = math.clamp(mouseY - offsetY, 0, 1080 - widget:GetHeight());
            widget:SetWorldPos(vec.New(posX, posY));
        end);
    end

    api.Refresh = function()
        local tooltipCurrentWidth, tooltipCurrentHeight = 0, 3;

        for _, v in state.tooltipLines do
            local linePosY = tooltipCurrentHeight;
            local isDoubleLine = v.lineRightText ~= nil;

            if v.lineLeftTextWidget == nil and v.lineLeftText:len() > 0  then
                v.lineLeftTextWidget = state.tooltipPanel:NewText(v.lineLeftText, vec.New(5, -linePosY), 0i, v.lineLeftTextTemplate);
                v.lineLeftTextWidget:SetEnabled(true);
                v.lineLeftTextWidget:SetAnchor(vec.New(0.0, 1.0));
                v.lineLeftTextWidget:SetRelativePoint(vec.New(0.0, 1.0));

                if v.lineLeftTextColor then
                    v.lineLeftTextWidget:SetColor(v.lineLeftTextColor);
                end
            end

            local textSize = v.lineLeftTextWidget:GetSize();
            local textWidth, textHeight = textSize.x, textSize.y;

            if isDoubleLine then
                if v.lineRightTextWidget == nil and v.lineRightText:len() > 0 then
                    v.lineRightTextWidget = state.tooltipPanel:NewText(v.lineRightText, vec.New(-5, -linePosY), 0i, v.lineRightTextTemplate);
                    v.lineRightTextWidget:SetEnabled(true);
                    v.lineRightTextWidget:SetAnchor(vec.New(1.0, 1.0));
                    v.lineRightTextWidget:SetRelativePoint(vec.New(1.0, 1.0));

                    if v.lineRightTextColor then
                        v.lineRightTextWidget:SetColor(v.lineRightTextColor);
                    end
                end

                local rightTextSize = v.lineRightTextWidget:GetSize();
                local rightTextWidth, rightTextHeight = rightTextSize.x, rightTextSize.y;

                textWidth += 5 + rightTextWidth;
                textHeight = math.max(textHeight, rightTextHeight);
            end

            tooltipCurrentWidth = math.max(tooltipCurrentWidth, textWidth);
            tooltipCurrentHeight += textHeight + v.lineTextHeightPadding;
        end

        tooltipCurrentWidth = math.max(tooltipMinWidth, tooltipCurrentWidth + 10);
        tooltipCurrentHeight += 5;

        state.tooltipPanel:SetSize(vec.New(tooltipCurrentWidth, tooltipCurrentHeight));
    end;

    api.SetItem = function(itemID)
        if itemID == 0i then
            return;
        end

        if state.tooltipPanelData.itemID == itemID then
            return;
        end

        local itemInfo : ItemInfo = Item.GetItemInfo(itemID);
        if itemInfo == nil then
            return;
        end

        api.ClearLines();
        state.tooltipPanelData.itemID = itemID;
        state.tooltipPanelData.spellID = 0i;

        local itemArmorTemplate     : ItemArmorInfo  | nil = nil;
        local itemWeaponTemplate    : ItemWeaponInfo | nil = nil;
        local itemShieldTemplate    : ItemShieldInfo | nil = nil;

        if itemInfo.ArmorTemplateID ~= 0i then
            itemArmorTemplate = Item.GetItemArmorInfo(itemInfo.ArmorTemplateID);
        elseif itemInfo.WeaponTemplateID ~= 0i then
            itemWeaponTemplate = Item.GetItemWeaponInfo(itemInfo.WeaponTemplateID);
        elseif itemInfo.ShieldTemplateID ~= 0i then
            itemShieldTemplate = Item.GetItemShieldInfo(itemInfo.ShieldTemplateID);
        end

        local itemRarityColor = ItemUtil.GetRarityColor(itemInfo.Rarity);
        local itemTypeName = ItemUtil.GetCategoryTypeText(itemInfo.Category, itemInfo.Type);
        local name = if itemInfo.Name:len() > 0 then itemInfo.Name else "Unknown";
        local itemBonusArmor = 0i;
        local itemBlock = 0i;
        api.AddLine(name, "GameTooltipLargeText", itemRarityColor);

        if itemInfo.VirtualLevel ~= 0i then
            local itemVirtualLevelText = "Item Level " .. tostring(itemInfo.VirtualLevel);
            api.AddLine(itemVirtualLevelText, "GameTooltipMediumText", state.yellowColor);
        end

        if itemInfo.Bind ~= 0i then
            local itemBindText = ItemUtil.GetBindTypeText(itemInfo.Bind);
            api.AddLine(itemBindText, "GameTooltipMediumText");
        end

        if itemInfo.Category == ItemUtil.Category.Container then
            if itemInfo.Durability ~= 0i then
                local itemDurabilityText = tostring(itemInfo.Durability) .. " Slot Bag";
                api.AddLine(itemDurabilityText, "GameTooltipMediumText");
            end
        else
            if itemArmorTemplate ~= nil then
                local equipTypeText = ItemUtil.GetEquipTypeText(itemArmorTemplate.EquipType);

                if itemInfo.Category == ItemUtil.Category.Armor
                    and integer.gt(itemInfo.Type, ItemUtil.CategoryType.Armor.Armor)
                then
                    local itemCategoryType = ItemUtil.GetCategoryTypeText(itemInfo.Category, itemInfo.Type);
                    api.AddDoubleLine(equipTypeText, itemCategoryType, "GameTooltipMediumText", "GameTooltipMediumText");
                else
                    api.AddLine(equipTypeText, "GameTooltipMediumText");
                end

                itemBonusArmor = itemArmorTemplate.BonusArmor;
            elseif itemWeaponTemplate ~= nil then
                local itemWeaponStyleText = ItemUtil.GetWeaponStyleTextFromWeaponCategoryType(itemInfo.Type);
                local itemSpeed = if itemWeaponTemplate.Speed > 0 then itemWeaponTemplate.Speed else 1.0;
                local itemSpeedText = "Speed " .. string.format("%.2f", tostring(itemWeaponTemplate.Speed));
                local itemDamageRangeText = tostring(itemWeaponTemplate.MinDamage) .. " - " .. tostring(itemWeaponTemplate.MaxDamage) .. " Damage";
                local minimumDamage = integer.tonumber(itemWeaponTemplate.MinDamage);
                local maximumDamage = integer.tonumber(itemWeaponTemplate.MaxDamage);
                local itemDps = ((minimumDamage + maximumDamage) / 2.0) / itemSpeed;
                local itemDpsText = "(" .. string.format("%.2f", tostring(itemDps)) .. " damage per second)";

                api.AddDoubleLine(itemWeaponStyleText,      itemTypeName,    "GameTooltipMediumText", "GameTooltipMediumText", state.whiteColor, state.whiteColor, 1);
                api.AddDoubleLine(itemDamageRangeText,      itemSpeedText,   "GameTooltipMediumText", "GameTooltipMediumText", state.whiteColor, state.whiteColor, 0);
                api.AddLine(itemDpsText,                                     "GameTooltipMediumText");

            elseif itemShieldTemplate ~= nil then
                itemBonusArmor = itemShieldTemplate.BonusArmor;
                itemBlock = itemShieldTemplate.Block;

                api.AddDoubleLine("Off Hand",      itemTypeName, "GameTooltipMediumText", "GameTooltipMediumText");
            end

            local itemArmor = integer.add(itemInfo.Armor, itemBonusArmor);
            if integer.gt(itemArmor, 0i) then
                local hasBonusArmor = integer.gt(itemBonusArmor, 0i);
                local itemArmorText = tostring(itemArmor) .. " Armor";
                local armorTextColor = if hasBonusArmor then vec.New(30.0/255.0, 255.0/255.0, 0.0/255.0) else nil;
                api.AddLine(itemArmorText, "GameTooltipMediumText", armorTextColor);
            end

            if integer.gt(itemBlock, 0i) then
                local shieldBlockText = tostring(itemBlock) .. " Block";
                api.AddLine(shieldBlockText, "GameTooltipMediumText");
            end

            local itemStatTemplateID = itemInfo.StatTemplateID;
            if itemStatTemplateID ~= 0i then
                local itemStats : ItemStatInfo = Item.GetItemStatInfo(itemStatTemplateID);
                local numStats = #itemStats;

                for i = 1, numStats do
                    local stat : ItemStat = itemStats[i];

                    local prefix = if integer.gt(stat.Value, 0i) then "+" else "";
                    local itemStatText = prefix .. tostring(stat.Value) .. " " .. ItemUtil.GetStatText(stat.ID);
                    api.AddLine(itemStatText, "GameTooltipMediumText");
                end
            end

            if itemInfo.Durability ~= 0i then
                local itemDurabilityText = "Durability " .. tostring(itemInfo.Durability) .. " / " .. tostring(itemInfo.Durability);
                api.AddLine(itemDurabilityText, "GameTooltipMediumText");
            end

            if itemInfo.RequiredLevel ~= 0i then
                local itemRequiredLevelText = "Requires Level " .. tostring(itemInfo.RequiredLevel);
                api.AddLine(itemRequiredLevelText, "GameTooltipMediumText");
            end

            local itemEffects = Item.GetItemEffects(itemID);
            local itemEffectTextColor = vec.New(30.0/255.0, 255.0/255.0, 0.0/255.0);
            for _, v in itemEffects do
                if integer.gt(v.Type, ItemEffectType.OnProc) then
                    continue;
                end

                local spellInfo = Spell.GetSpellInfo(v.SpellID);

                if spellInfo.Description:len() > 0 then
                    local typePrefix = ItemUtil.GetItemEffectTypePrefix(integer.add(v.Type, 1i));
                    local effectText = typePrefix .. spellInfo.Description;
                    api.AddLine(effectText, "GameTooltipMediumText", itemEffectTextColor, 1);
                end
            end
        end

        if itemInfo.Description:len() > 0 then
            api.AddLine(itemInfo.Description, "GameTooltipMediumText", state.yellowColor);
        end

        api.AddDoubleLine("ItemID", tostring(itemID), "GameTooltipDebugText", "GameTooltipDebugText", state.yellowColor, state.whiteColor, 2);
        api.AddDoubleLine("IconID", tostring(itemInfo.IconID), "GameTooltipDebugText", "GameTooltipDebugText", state.yellowColor, state.whiteColor, 2);
        api.AddDoubleLine("DisplayID", tostring(itemInfo.DisplayID), "GameTooltipDebugText", "GameTooltipDebugText", state.yellowColor, state.whiteColor);
        api.Refresh();
    end;

    api.SetSpell = function(spellID : integer)
        if spellID == 0i or state.tooltipPanelData.spellID == spellID then return; end
        local spellInfo : SpellInfo = Spell.GetSpellInfo(spellID);
        if spellInfo == nil then return; end

        api.ClearLines();
        state.tooltipPanelData.itemID = 0i;
        state.tooltipPanelData.spellID = spellID;
        api.AddLine(spellInfo.Name, "GameTooltipLargeText", state.whiteColor);
        if spellInfo.Description:len() > 0 then
            api.AddLine(spellInfo.Description, "GameTooltipMediumText", state.yellowColor);
        end
        api.AddDoubleLine("SpellID", tostring(spellID), "GameTooltipDebugText", "GameTooltipDebugText", state.yellowColor, state.whiteColor);
        api.Refresh();
    end;

    api.AddLine = function(text : string, textTemplate : string, textColor : vec?, textHeightPadding : number?)
        local heightPadding = if textHeightPadding then textHeightPadding else 3;
        local line : TooltipLine = { lineLeftText = text, lineRightText = nil, lineLeftTextTemplate = textTemplate, lineRightTextTemplate = nil, lineLeftTextColor = textColor, lineRightTextColor = nil, lineLeftTextWidget = nil, lineRightTextWidget = nil, lineTextHeightPadding = heightPadding };
        table.insert(state.tooltipLines, line);
    end;

    api.AddDoubleLine = function(leftText : string, rightText : string, leftTextTemplate : string, rightTextTemplate : string, leftTextColor : vec?, rightTextColor : vec?, textHeightPadding : number?)
        local heightPadding = if textHeightPadding then textHeightPadding else 3;
        local line : TooltipLine = { lineLeftText = leftText, lineRightText = rightText, lineLeftTextTemplate = leftTextTemplate, lineRightTextTemplate = rightTextTemplate, lineLeftTextColor = leftTextColor, lineRightTextColor = rightTextColor, lineLeftTextWidget = nil, lineRightTextWidget = nil, lineTextHeightPadding = heightPadding };
        table.insert(state.tooltipLines, line);
    end;

    api.ClearLines = function()
        local numTooltipLines = #state.tooltipLines;
        if numTooltipLines > 0 then
            for _, v in state.tooltipLines do
                if v.lineLeftTextWidget then
                    UI.DestroyWidget(v.lineLeftTextWidget);
                end

                if v.lineRightTextWidget then
                    UI.DestroyWidget(v.lineRightTextWidget);
                end
            end
            table.clear(state.tooltipLines);
        end
        state.tooltipPanelData.itemID = 0i;
        state.tooltipPanelData.spellID = 0i;
    end;

    api.SetPosition = function(position : vec)
        local posX = math.clamp(position.x, 0, 1920 - state.tooltipPanel:GetWidth());
        local posY = math.clamp(position.y, 0, 1080 - state.tooltipPanel:GetHeight());
        state.tooltipPanel:SetWorldPos(vec.New(posX, posY));
    end;

    api.SetPositionAroundCursor = function(cursorPosition : vec)
        local minimum = vec.New(0, 0);
        local maximum = vec.New(1920, 1080);
        if type(parent) ~= "string" then
            local success, parentPosition, parentSize = pcall(function() return parent:GetWorldPos(), parent:GetSize(); end);
            if success then
                minimum = parentPosition;
                maximum = parentPosition + parentSize;
            end
        end

        local tooltipSize = state.tooltipPanel:GetSize();
        local posX = cursorPosition.x + 6;
        if posX + tooltipSize.x > maximum.x then posX = cursorPosition.x - 6 - tooltipSize.x; end
        posX = math.clamp(posX, minimum.x, maximum.x - tooltipSize.x);

        local posY = cursorPosition.y + 10;
        if posY + tooltipSize.y > maximum.y then posY = cursorPosition.y - 32 - tooltipSize.y; end
        posY = math.clamp(posY, minimum.y, maximum.y - tooltipSize.y);
        state.tooltipPanel:SetWorldPos(vec.New(posX, posY));
    end;

    api.Show = function()
        state.tooltipPanel:SetEnabled(true);
        state.tooltipPanelData.isEnabled = true;
    end;

    api.Hide = function()
        state.tooltipPanel:SetEnabled(false);
        state.tooltipPanelData.isEnabled = false;
    end;

    api.Toggle = function()
        local isEnabled = state.tooltipPanelData.isEnabled;

        if isEnabled then
            api.Hide();
        else
            api.Show();
        end
    end;

    api.IsShown = function()
        return state.tooltipPanelData.isEnabled;
    end;

    api.ContainsItem = function(itemID : integer)
        return itemID == state.tooltipPanelData.itemID;
    end;

    return api;
end

local defaultTooltip = CreateTooltip("GameTooltip") :: GameTooltipAPI;
defaultTooltip.New = function(parent : (Canvas | Widget))
    local tooltip = CreateTooltip(parent);
    tooltip:Initialize();
    return tooltip;
end;

return defaultTooltip;
