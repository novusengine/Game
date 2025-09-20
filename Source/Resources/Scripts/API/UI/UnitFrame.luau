local OptionsContext = require("@src/API/OptionsContext")

export type UnitFrameAPI = 
{
    NewUnitFrame : (parent : Widget, posX : number, posY : number, sizeX : number, sizeY : number, layer : number, unitFrameTemplateTable : UnitFrameTemplate) -> UnitFrame,

    unitFrames : { UnitFrame }
}

export type UnitFrameTemplate =
{
    unitID : number,
    unitName : string
}

export type UnitFrame = {
    -- Fields
    unitID: number,
    unitName: string,

    health: number,
    maxHealth: number,
    resource: number,
    maxResource: number,

    border: Panel,
    background: Panel,
    healthFill: Panel,
    resourceFill: Panel,

    nameText: TextWidget,
    healthText: TextWidget,
    resourceText: TextWidget,

    -- Methods
    Destroy: (self: UnitFrame) -> (),

    IsEnabled: (self: UnitFrame) -> boolean,
    SetEnabled: (self: UnitFrame, enabled: boolean) -> (),

    IsVisible: (self: UnitFrame) -> boolean,
    SetVisible: (self: UnitFrame, visible: boolean) -> (),

    IsInteractable: (self: UnitFrame) -> boolean,
    SetInteractable: (self: UnitFrame, interactable: boolean) -> (),

    IsFocusable: (self: UnitFrame) -> boolean,
    SetFocusable: (self: UnitFrame, focusable: boolean) -> (),
    
    SetAnchor: (self: UnitFrame, anchorX: number, anchorY: number) -> (),
    SetRelativePoint: (self: UnitFrame, relativeX: number, relativeY: number) -> (),

    GetUnitID: (self: UnitFrame) -> number,
    SetUnitID: (self: UnitFrame, unitID: number) -> (),
    GetUnitName: (self: UnitFrame) -> string,
    SetUnitName: (self: UnitFrame, unitName: string) -> (),
    
    GetHealth: (self: UnitFrame) -> number,
    SetHealth: (self: UnitFrame, health: number) -> (),
    GetMaxHealth: (self: UnitFrame) -> number,
    SetMaxHealth: (self: UnitFrame, maxHealth: number) -> (),
    UpdateHealth: (self: UnitFrame) -> (),
    
    GetResource: (self: UnitFrame) -> number,
    SetResource: (self: UnitFrame, resource: number) -> (),
    GetMaxResource: (self: UnitFrame) -> number,
    SetMaxResource: (self: UnitFrame, maxResource: number) -> (),
    UpdateResource: (self: UnitFrame) -> (),
}

local unitFrameAPI : UnitFrameAPI = 
{
    unitFrames = {}
};

local function SetupUnitFrameMethods(unitFrameTable : UnitFrame)
    unitFrameTable.Destroy = function(self : UnitFrame)
        UI.DestroyWidget(self.border);
    end

    unitFrameTable.IsEnabled = function(self : UnitFrame)
        return self.border:IsEnabled();
    end
    unitFrameTable.SetEnabled = function(self : UnitFrame, enabled : boolean)
        unitFrameTable.border:SetEnabled(enabled);
    end

    unitFrameTable.IsVisible = function(self : UnitFrame)
        return self.border:IsVisible();
    end
    unitFrameTable.SetVisible = function(self : UnitFrame, visible : boolean)
        unitFrameTable.border:SetVisible(visible);
    end

    unitFrameTable.IsInteractable = function(self : UnitFrame)
        return self.border:IsInteractable();
    end
    unitFrameTable.SetInteractable = function(self : UnitFrame, interactable : boolean)
        unitFrameTable.border:SetInteractable(interactable);
    end

    unitFrameTable.IsFocusable = function(self : UnitFrame)
        return self.border:IsFocusable();
    end
    unitFrameTable.SetFocusable = function(self : UnitFrame, focusable : boolean)
        unitFrameTable.border:SetFocusable(focusable);
    end

    unitFrameTable.SetAnchor = function(self : UnitFrame, anchorX : number, anchorY : number)
        self.border:SetAnchor(anchorX, anchorY);
    end

    unitFrameTable.SetRelativePoint = function(self : UnitFrame, relativeX : number, relativeY : number)
        self.border:SetRelativePoint(relativeX, relativeY);
    end

    unitFrameTable.GetUnitID = function(self : UnitFrame)
        return self.unitID;
    end

    unitFrameTable.SetUnitID = function(self : UnitFrame, unitID : number)
        self.unitID = unitID;
    end

    unitFrameTable.GetUnitName = function(self : UnitFrame)
        return self.unitName;
    end

    unitFrameTable.SetUnitName = function(self : UnitFrame, unitName : string)
        self.unitName = unitName;
        self.nameText:SetText(unitName);
    end

    unitFrameTable.GetHealth = function(self : UnitFrame)
        return self.health; 
    end
    unitFrameTable.SetHealth = function(self : UnitFrame, health : number)
        self.health = health;
        self:UpdateHealth();
    end
    unitFrameTable.GetMaxHealth = function(self : UnitFrame)
        return self.maxHealth;
    end
    unitFrameTable.SetMaxHealth = function(self : UnitFrame, maxHealth : number)
        self.maxHealth = maxHealth;
        self:UpdateHealth();
    end
    unitFrameTable.UpdateHealth = function(self : UnitFrame)
        local healthPercent = (self.health / self.maxHealth);
        local newWidth = self.background:GetWidth() * healthPercent;
        --unitFrameTable.healthFill:SetClipRect(0, 0, 1, 1);
        self.healthFill:SetWidth(newWidth);

        local text = string.format("%d/%d", self.health, self.maxHealth);
        unitFrameTable.healthText:SetText(text);
    end

    unitFrameTable.GetResource = function(self : UnitFrame)
        return self.resource;
    end
    unitFrameTable.SetResource = function(self : UnitFrame, resource : number)
        self.resource = resource;
        self:UpdateResource();
    end
    unitFrameTable.GetMaxResource = function(self : UnitFrame)
        return self.maxResource;
    end
    unitFrameTable.SetMaxResource = function(self : UnitFrame, maxResource : number)
        self.maxResource = maxResource;
        self:UpdateResource();
    end
    unitFrameTable.UpdateResource = function(self : UnitFrame)
        local resourcePercent = (self.resource / self.maxResource);
        local newWidth = self.background:GetWidth() * resourcePercent;
        --unitFrameTable.resourceFill:SetClipRect(0, 0, 1, 1);
        self.resourceFill:SetWidth(newWidth);

        local text = string.format("%d/%d", self.resource, self.maxResource);
        unitFrameTable.resourceText:SetText(text);
    end

    return unitFrameTable;
end

function unitFrameAPI.NewUnitFrame(parent : Widget, posX : number, posY : number, sizeX : number, sizeY : number, layer : number, unitFrameTemplateTable : UnitFrameTemplate)
    local unitFrameTable : UnitFrame = {};

    unitFrameTable.unitID = unitFrameTemplateTable.unitID or -1;
    unitFrameTable.unitName = unitFrameTemplateTable.unitName or "Unknown";
    unitFrameTable.health = unitFrameTemplateTable.unitHealth or 50;
    unitFrameTable.maxHealth = unitFrameTemplateTable.unitMaxHealth or 100;
    unitFrameTable.resource = unitFrameTemplateTable.unitResource or 75;
    unitFrameTable.maxResource = unitFrameTemplateTable.unitMaxResource or 100;

    local borderWidth = 2;

    -- Create the border panel
    unitFrameTable.border = parent:NewPanel(posX, posY, sizeX, sizeY, layer);
    unitFrameTable.border:SetRelativePoint(0.5, 0.5);
    unitFrameTable.border:SetColor(vector.create(0.0, 0.0, 0.0));

    -- Create the background panel
    local sizeXWithoutBorder = sizeX - borderWidth;
    local sizeYWithoutBorder = sizeY - borderWidth;

    unitFrameTable.background = unitFrameTable.border:NewPanel(0, 0, sizeXWithoutBorder, sizeYWithoutBorder, layer, "DebugDarkGrey");
    unitFrameTable.background:SetBackground("Data/Texture/interface/raidframe/raid-bar-hp-fill.dds");
    unitFrameTable.background:SetAnchor(0.5, 0.5);
    unitFrameTable.background:SetRelativePoint(0.5, 0.5);

    -- Create name text
    unitFrameTable.nameText = unitFrameTable.border:NewText(unitFrameTable.unitName, 3, 0, layer + 1, "UnitFrameName");
    unitFrameTable.nameText:SetAnchor(0.0, 1.0);
    unitFrameTable.nameText:SetRelativePoint(0.0, 1.0);

    local totalAvailableHeight = sizeYWithoutBorder;
    local healthBarHeight = math.floor(totalAvailableHeight * 0.65);
    local resourceBarHeight = math.floor(totalAvailableHeight * 0.35);

    -- Create Health Fill
    unitFrameTable.healthFill = unitFrameTable.background:NewPanel(0, resourceBarHeight, sizeXWithoutBorder, healthBarHeight, 0, "UnitFrameHealthFill");
    unitFrameTable.healthFill:SetBackground("Data/Texture/interface/raidframe/raid-bar-hp-fill.dds");
    unitFrameTable.healthFill:SetRelativePoint(0.0, 0.0);
    unitFrameTable.healthFill:SetClipRect(0, 0, 1, 1);

    -- Create Resource Fill
    unitFrameTable.resourceFill = unitFrameTable.background:NewPanel(0, -2, sizeXWithoutBorder, resourceBarHeight + 1, 0, "UnitFrameResourceFill");
    unitFrameTable.resourceFill:SetBackground("Data/Texture/interface/raidframe/raid-bar-resource-fill.dds");
    unitFrameTable.resourceFill:SetRelativePoint(0.0, 0.0);
    unitFrameTable.resourceFill:SetClipRect(0, 0, 1, 1);

    -- Create Health Text
    local healthTextYOffset = math.floor(resourceBarHeight / 2.0);
    unitFrameTable.healthText = unitFrameTable.border:NewText("1000", 0, healthTextYOffset, 0, "UnitFrameHealth");
    unitFrameTable.healthText:SetAnchor(0.5, 0.5);
    unitFrameTable.healthText:SetRelativePoint(0.5, 0.5);

    -- Create Resource Text
    local resourceTextYOffset = -math.floor(healthBarHeight / 2.0);
    unitFrameTable.resourceText = unitFrameTable.border:NewText("1000", 0, resourceTextYOffset, 0, "UnitFrameResource");
    unitFrameTable.resourceText:SetAnchor(0.5, 0.5);
    unitFrameTable.resourceText:SetRelativePoint(0.5, 0.5);

    unitFrameTable = SetupUnitFrameMethods(unitFrameTable);
    unitFrameTable:UpdateHealth();
    unitFrameTable:UpdateResource();

    return unitFrameTable;
end

return unitFrameAPI;