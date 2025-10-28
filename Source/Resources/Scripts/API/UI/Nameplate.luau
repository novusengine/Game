local OptionsContext = require("@src/API/OptionsContext")

type NameplateAPI = 
{
    NewNameplate : (parent : Panel, posX : number, posY : number, sizeX : number, sizeY : number, layer : number, nameplateTemplateTable : NameplateTemplate) -> Nameplate,

    nameplates : { Nameplate },
    nameplateTextDisplayMode : NameplateTextDisplayType,
}

type NameplateTemplate =
{
    unitID : number,
}

type Nameplate = {
    -- Fields
    unitID: number,
    unitName: string,
    isStandalone: boolean,
    health: number,
    maxHealth: number,
    border: Panel,
    background: Panel,
    fill: Panel,
    nameText: TextWidget,
    nameTextColor: vector,
    healthText: TextWidget,
    healthTextColor: vector,
    standaloneNameText: TextWidget,
    standaloneNameTextColor: vector,

    -- Methods
    Destroy: (self: Nameplate) -> (),
    SetEnabled: (self: Nameplate, enabled: boolean) -> (),
    SetVisible: (self: Nameplate, visible: boolean) -> (),
    SetInteractable: (self: Nameplate, interactable: boolean) -> (),
    SetFocusable: (self: Nameplate, focusable: boolean) -> (),
    SetAnchor: (self: Nameplate, anchorX: number, anchorY: number) -> (),
    SetRelativePoint: (self: Nameplate, relativeX: number, relativeY: number) -> (),
    SetPosToUnit: (self: Nameplate, unitID: number) -> (),
    GetUnitID: (self: Nameplate) -> number,
    GetUnitName: (self: Nameplate) -> string,
    SetUnitName: (self: Nameplate, unitName: string) -> (),
    IsStandalone: (self: Nameplate) -> boolean,
    SetStandalone: (self: Nameplate, standalone: boolean) -> (),
    GetHealth: (self: Nameplate) -> number,
    SetHealth: (self: Nameplate, health: number) -> (),
    GetMaxHealth: (self: Nameplate) -> number,
    SetMaxHealth: (self: Nameplate, maxHealth: number) -> (),
    UpdateHealth: (self: Nameplate) -> (),
}

local nameplateTextDisplayType : NameplateTextDisplayType = {
    None = 1,
    Value = 2,
    ValuePercent = 3,
}
NameplateTextDisplayType = nameplateTextDisplayType;

local nameplateAPI : NameplateAPI = 
{
    nameplates = {},
    nameplateTextDisplayMode = NameplateTextDisplayType.ValuePercent,
};

local function FormatText(health : number, healthPercent : number)
    -- Setup options
    local optionCategory = OptionsContext:GetCategory(OptionCategoryType.Gameplay);
    local optionGroup = optionCategory:Get("Nameplates");
    local optionSection = optionGroup:Get("General");

    local mode = NameplateTextDisplayType.None;
    if (optionSection.options:Has("Text Display Mode")) then
        local textDisplayModeOption = optionSection:GetOption("Text Display Mode");
        mode = textDisplayModeOption:Get();
    end

    if mode == NameplateTextDisplayType.Value then
        return string.format("%d", health);
    elseif mode == NameplateTextDisplayType.ValuePercent then
        return string.format("%d (%.0f%%)", health, healthPercent * 100);
    end

    return "";
end

local function SetupNameplateMethods(nameplateTable : Nameplate)
    nameplateTable.Destroy = function(self : Nameplate)
        UI.DestroyWidget(self.border);
        UI.DestroyWidget(self.standaloneNameText);
    end

    nameplateTable.SetEnabled = function(self : Nameplate, enabled : boolean)
        nameplateTable.border:SetEnabled(enabled);
    end

    nameplateTable.SetVisible = function(self : Nameplate, visible : boolean)
        nameplateTable.border:SetVisible(visible);
    end

    nameplateTable.SetInteractable = function(self : Nameplate, interactable : boolean)
        nameplateTable.border:SetInteractable(interactable);
    end

    nameplateTable.SetFocusable = function(self : Nameplate, focusable : boolean)
        nameplateTable.border:SetFocusable(focusable);
    end

    nameplateTable.SetAnchor = function(self : Nameplate, anchorX : number, anchorY : number)
        self.border:SetAnchor(anchorX, anchorY);
    end

    nameplateTable.SetRelativePoint = function(self : Nameplate, relativeX : number, relativeY : number)
        self.border:SetRelativePoint(relativeX, relativeY);
    end

    nameplateTable.SetPosToUnit = function(self : Nameplate, unitID : number)
        if self.isStandalone then
            local resultCode = Unit.SetWidgetToNamePos(self.standaloneNameText, unitID);

            if resultCode ~= 2 then
                local result = if resultCode == 1 then true else false;
                self.standaloneNameText:SetVisible(result);
            end
        else
            local borderResultCode = Unit.SetWidgetToNamePos(self.border, unitID);
            local backgroundResultCode = Unit.SetWidgetToNamePos(self.background, unitID);
            local fillResultCode = Unit.SetWidgetToNamePos(self.fill, unitID);
            local nameResultCode = Unit.SetWidgetToNamePos(self.nameText, unitID);

            if borderResultCode ~= 2 then
                local result = if borderResultCode == 1 then true else false;
                self.border:SetVisible(result);
            end

            if backgroundResultCode ~= 2 then
                local result = if backgroundResultCode == 1 then true else false;
                self.background:SetVisible(result);
            end

            if fillResultCode ~= 2 then
                local result = if fillResultCode == 1 then true else false;
                self.fill:SetVisible(result);
            end

            if nameResultCode ~= 2 then
                local result = if nameResultCode == 1 then true else false;
                self.nameText:SetVisible(result);
            end
        end

        local widget = if self.isStandalone then self.standaloneNameText else self.border;
        local resultCode = Unit.SetWidgetToNamePos(widget, unitID);

        if resultCode ~= 2 then
            local result = if resultCode == 1 then true else false;
            widget:SetVisible(result);
        end
    end

    nameplateTable.GetUnitID = function(self : Nameplate)
        return self.unitID;
    end

    nameplateTable.GetUnitName = function(self : Nameplate)
        return self.unitName;
    end

    nameplateTable.SetUnitName = function(self : Nameplate, unitName : string)
        self.unitName = unitName;
        self.nameText:SetText(unitName);
        self.standaloneNameText:SetText(unitName);
    end

    nameplateTable.IsStandalone = function(self : Nameplate)
        return self.isStandalone;
    end

    nameplateTable.SetStandalone = function(self : Nameplate, standalone : boolean)
        self.isStandalone = standalone;

        if standalone then
            self.standaloneNameText:SetVisible(true);
            self.border:SetVisible(false);
        else
            self.standaloneNameText:SetVisible(false);
            self.border:SetVisible(true);
        end
    end

    nameplateTable.GetHealth = function(self : Nameplate)
        return self.health; 
    end

    nameplateTable.SetHealth = function(self : Nameplate, health : number)
        self.health = health;
        self:UpdateHealth();
    end

    nameplateTable.GetMaxHealth = function(self : Nameplate)
        return self.maxHealth;
    end

    nameplateTable.SetMaxHealth = function(self : Nameplate, maxHealth : number)
        self.maxHealth = maxHealth;
        self:UpdateHealth();
    end

    nameplateTable.UpdateHealth = function(self : Nameplate)
        if self:IsStandalone() then
            return;
        end

        local healthPercent = (self.health / self.maxHealth);
        local newWidth = self.background:GetWidth() * healthPercent;
        --nameplateTable.fill:SetClipRect(0, 0, 1, 1);
        self.fill:SetWidth(newWidth);

        local text = FormatText(self.health, healthPercent);--string.format("%d (%.0f%%)", self.health, healthPercent * 100);
        nameplateTable.healthText:SetText(text);
    end

    nameplateTable.SetHighlighted = function(self : Nameplate, highlighted : boolean)

        local baseStandaloneColor = if highlighted then self.standaloneNameTextColor * vector.create(1.25, 1.25, 1.25) else self.standaloneNameTextColor;
        baseStandaloneColor = vector.min(baseStandaloneColor, vector.create(1.0, 1.0, 1.0));

        local baseNameColor = if highlighted then self.nameTextColor * vector.create(1.25, 1.25, 1.25) else self.nameTextColor;
        baseNameColor = vector.min(baseNameColor, vector.create(1.0, 1.0, 1.0));

        local baseHealthColor = if highlighted then self.healthTextColor * vector.create(1.25, 1.25, 1.25) else self.healthTextColor;
        baseHealthColor = vector.min(baseHealthColor, vector.create(1.0, 1.0, 1.0));

        self.standaloneNameText:SetColor(baseStandaloneColor);
        self.nameText:SetColor(baseNameColor);
        self.healthText:SetColor(baseHealthColor);
    end

    return nameplateTable;
end

function nameplateAPI.NewNameplate(parent : Widget, posX : number, posY : number, sizeX : number, sizeY : number, layer : number, nameplateTemplateTable : NameplateTemplate)
    local nameplateTable : Nameplate = {};

    nameplateTable.unitID = nameplateTemplateTable.unitID or -1;
    nameplateTable.unitName = nameplateTemplateTable.unitName or "Unknown";
    nameplateTable.isStandalone = nameplateTemplateTable.isStandalone or true;
    nameplateTable.health = 0;
    nameplateTable.maxHealth = 100;

    local borderWidth = 2;

    -- Create the border panel
    nameplateTable.border = parent:NewPanel(posX, posY, sizeX, sizeY, layer);
    nameplateTable.border:SetRelativePoint(0.5, 0.5);
    nameplateTable.border:SetColor(vector.create(0.0, 0.0, 0.0));

    -- Create the background panel
    local sizeXWithoutBorder = sizeX - borderWidth;
    local sizeYWithoutBorder = sizeY - borderWidth;

    nameplateTable.background = nameplateTable.border:NewPanel(0, 0, sizeXWithoutBorder, sizeYWithoutBorder, layer, "DebugRed");
    nameplateTable.background:SetBackground("Data/Texture/interface/raidframe/raid-bar-hp-fill.dds");
    nameplateTable.background:SetAnchor(0.5, 0.5);
    nameplateTable.background:SetRelativePoint(0.5, 0.5);

    -- Create the fill panel which gets resized based on HP
    local fillPosX = (-sizeXWithoutBorder * 0.5) - 0.5;
    
    nameplateTable.fill = nameplateTable.background:NewPanel(fillPosX, 0, sizeXWithoutBorder, sizeYWithoutBorder, layer, "DebugGreen");
    nameplateTable.fill:SetBackground("Data/Texture/interface/raidframe/raid-bar-hp-fill.dds");
    nameplateTable.fill:SetRelativePoint(0.0, 0.5);
    nameplateTable.fill:SetClipRect(0, 0, 1, 1);

    -- Create name text
    nameplateTable.nameText = nameplateTable.border:NewText(nameplateTable.unitName, 5, 2, 0, "NameplateName");
    nameplateTable.nameText:SetAnchor(0.0, 1.0);
    nameplateTable.nameText:SetRelativePoint(0.0, 0.0);
    nameplateTable.nameTextColor = nameplateTable.nameText:GetColor();

    -- Create health text
    nameplateTable.healthText = nameplateTable.background:NewText("1000 (100%)", 0, 3, 0, "NameplateHealth");
    nameplateTable.healthText:SetAnchor(1.0, 0.5);
    nameplateTable.healthText:SetRelativePoint(1.0, 0.5);
    nameplateTable.healthTextColor = nameplateTable.healthText:GetColor();

    -- Create standalone name text
    nameplateTable.standaloneNameText = parent:NewText(nameplateTable.unitName, 5, 2, 0, "PlayerName");
    nameplateTable.standaloneNameText:SetRelativePoint(0.5, 0.0);
    nameplateTable.standaloneNameTextColor = nameplateTable.standaloneNameText:GetColor();

    nameplateTable = SetupNameplateMethods(nameplateTable);

    nameplateTable:SetStandalone(nameplateTable.isStandalone);

    return nameplateTable;
end

return nameplateAPI;