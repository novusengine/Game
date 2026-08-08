local RootedWidget = require("@src/API/UI/RootedWidget")
local UnitPresentation = require("@src/API/Game/UI/UnitPresentation")

export type NameplateAPI =
{
    New : (parent : Widget, position : vec, size : vec, layer : integer, nameplateTemplateTable : NameplateTemplate) -> Nameplate,

    nameplates : { Nameplate },
}

export type NameplateTemplate =
{
    unitID : integer,
    unitName : string?,
    isStandalone : boolean?,
}

export type Nameplate = {
    -- Fields
    root: Widget,
    unitID: integer,
    unitName: string,
    isStandalone: boolean,
    health: number,
    maxHealth: number,
    background: Panel,
    fill: Panel,
    nameText: TextWidget,
    healthText: TextWidget,

    -- Methods
    Destroy: (self: Nameplate) -> (),
    SetEnabled: (self: Nameplate, enabled: boolean) -> (),
    SetVisible: (self: Nameplate, visible: boolean) -> (),
    SetInteractable: (self: Nameplate, interactable: boolean) -> (),
    SetFocusable: (self: Nameplate, focusable: boolean) -> (),
    SetAnchor: (self: Nameplate, anchor: vec) -> (),
    SetRelativePoint: (self: Nameplate, point: vec) -> (),
    SetPosToUnit: (self: Nameplate, unitID: integer) -> (),
    GetUnitID: (self: Nameplate) -> integer,
    GetUnitName: (self: Nameplate) -> string,
    SetUnitName: (self: Nameplate, unitName: string) -> (),
    SetNameColor: (self: Nameplate, color: vec) -> (),
    IsStandalone: (self: Nameplate) -> boolean,
    SetStandalone: (self: Nameplate, standalone: boolean) -> (),
    SetHealthValues: (self: Nameplate, health: number, maxHealth: number) -> (),
    UpdateHealth: (self: Nameplate) -> (),
}

local nameplateAPI : NameplateAPI =
{
    nameplates = {},
};

local NAME_OFFSET_Y = 2.0;

local function FormatHealthText(health : number, maxHealth : number, healthPercent : number)
    return string.format("%d / %d (%.0f%%)", health, maxHealth, healthPercent * 100);
end

local function SetupNameplateMethods(nameplateTable : Nameplate)
    nameplateTable.Destroy = function(self : Nameplate)
        UI.DestroyWidget(self.root);
    end

    nameplateTable.SetEnabled = function(self : Nameplate, enabled : boolean)
        self.root:SetEnabled(enabled);
    end

    nameplateTable.SetVisible = function(self : Nameplate, visible : boolean)
        self.root:SetVisible(visible);
        self.background:SetVisible(visible and not self.isStandalone);
        self.nameText:SetVisible(visible);
    end

    nameplateTable.SetInteractable = function(self : Nameplate, interactable : boolean)
        self.background:SetInteractable(interactable);
    end

    nameplateTable.SetFocusable = function(self : Nameplate, focusable : boolean)
        self.background:SetFocusable(focusable);
    end

    nameplateTable.SetAnchor = function(self : Nameplate, anchor : vec)
        self.root:SetAnchor(anchor);
    end

    nameplateTable.SetRelativePoint = function(self : Nameplate, point : vec)
        self.root:SetRelativePoint(point);
    end

    nameplateTable.SetPosToUnit = function(self : Nameplate, unitID : integer)
        local resultCode = Unit.SetWidgetToNamePos(self.root, unitID);
        self:SetVisible(resultCode == 1i);
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
    end

    nameplateTable.SetNameColor = function(self : Nameplate, color : vec)
        self.nameText:SetColor(color);
    end

    nameplateTable.IsStandalone = function(self : Nameplate)
        return self.isStandalone;
    end

    nameplateTable.SetStandalone = function(self : Nameplate, standalone : boolean)
        self.isStandalone = standalone;
        self.background:SetVisible(not standalone);
        self.nameText:SetVisible(true);
        self.nameText:SetPosY(if standalone then NAME_OFFSET_Y else NAME_OFFSET_Y + self.background:GetHeight());
    end

    nameplateTable.SetHealthValues = function(self : Nameplate, health : number, maxHealth : number)
        self.health = health;
        self.maxHealth = maxHealth;
        self:UpdateHealth();
    end

    nameplateTable.UpdateHealth = function(self : Nameplate)
        local healthPercent = if self.maxHealth > 0 then math.clamp(self.health / self.maxHealth, 0.0, 1.0) else 0.0;
        local newWidth = self.background:GetWidth() * healthPercent;
        self.fill:SetWidth(newWidth);
        self.fill:SetColor(UnitPresentation.GetHealthFillColor(healthPercent), 1.0);

        self.healthText:SetText(FormatHealthText(self.health, self.maxHealth, healthPercent));
    end

    return nameplateTable;
end

function nameplateAPI.New(parent : Widget, position : vec, size : vec, layer : integer, nameplateTemplateTable : NameplateTemplate)
    local nameplateTable : Nameplate = {} :: any;

    nameplateTable.unitID = nameplateTemplateTable.unitID or -1i;
    nameplateTable.unitName = nameplateTemplateTable.unitName or "Unknown";
    nameplateTable.isStandalone = nameplateTemplateTable.isStandalone == true;
    nameplateTable.health = 0;
    nameplateTable.maxHealth = 100;

    -- Keep the shared name and optional health bar in the same 3D-positioned subtree.
    nameplateTable.root = parent:NewWidget(position, layer);
    RootedWidget.Attach(nameplateTable);

    -- The root is 3D-positioned, so center the bar with an explicit local offset. Avoid borders and
    -- half-pixel insets here: projected UI is snapped to the pixel grid and those edges can shimmer.
    nameplateTable.background = nameplateTable.root:NewPanel(vec.New(-size.x * 0.5, 0), size, 0i, "NameplateHealthBackground");

    -- Create the fill panel which gets resized based on HP
    nameplateTable.fill = nameplateTable.background:NewPanel(vec.New(0, 0), size, 0i, "NameplateHealthFill");

    -- Create name text
    nameplateTable.nameText = nameplateTable.root:NewText(nameplateTable.unitName, vec.New(0, NAME_OFFSET_Y), 0i, "NameplateName");
    nameplateTable.nameText:SetRelativePoint(vec.New(0.5, 0.0));

    -- Create health text
    nameplateTable.healthText = nameplateTable.background:NewText("0 / 100 (0%)", vec.New(0, 0), 0i, "NameplateHealth");
    nameplateTable.healthText:SetAnchor(vec.New(0.5, 0.5));
    nameplateTable.healthText:SetRelativePoint(vec.New(0.5, 0.5));

    nameplateTable = SetupNameplateMethods(nameplateTable);

    nameplateTable:SetStandalone(nameplateTable.isStandalone);

    return nameplateTable;
end

return nameplateAPI;
