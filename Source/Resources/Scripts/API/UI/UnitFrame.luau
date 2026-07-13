local OptionsContext = require("@src/API/OptionsContext")

export type UnitFrameAPI = 
{
    NewUnitFrame : (parent : Widget, posX : number, posY : number, sizeX : number, sizeY : number, layer : number, unitFrameTemplateTable : UnitFrameTemplate) -> UnitFrame,

    unitFrames : { UnitFrame }
}

export type UnitFrameTemplate =
{
    unitID : number,
    unitName : string,
    unitLevel : number?,
    unitHealth : number?,
    unitMaxHealth : number?,
    unitResource : number?,
    unitMaxResource : number?
}

export type UnitFrame = {
    -- Fields
    unitID: number,
    unitName: string,
    unitLevel: number,

    health: number,
    maxHealth: number,
    resource: number,
    maxResource: number,
    displayedHealthPercent: number,
    displayedResourcePercent: number,
    isSelected: boolean,
    healthFillColor: vector,

    border: Panel,
    background: Panel,
    levelBackground: Panel,
    levelFill: Panel,
    healthBackground: Panel,
    resourceBackground: Panel,
    healthFill: Panel,
    resourceFill: Panel,

    nameText: TextWidget,
    levelText: TextWidget,
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

    SetSelected: (self: UnitFrame, selected: boolean) -> (),
    UpdateVisualState: (self: UnitFrame) -> (),
    UpdateBarFills: (self: UnitFrame, deltaTime: number, smooth: boolean) -> (),
    
    SetAnchor: (self: UnitFrame, anchorX: number, anchorY: number) -> (),
    SetRelativePoint: (self: UnitFrame, relativeX: number, relativeY: number) -> (),

    GetUnitID: (self: UnitFrame) -> number,
    SetUnitID: (self: UnitFrame, unitID: number) -> (),
    GetUnitName: (self: UnitFrame) -> string,
    SetUnitName: (self: UnitFrame, unitName: string) -> (),
    SetUnitLevel: (self: UnitFrame, unitLevel: number) -> (),
    
    GetHealth: (self: UnitFrame) -> number,
    SetHealth: (self: UnitFrame, health: number) -> (),
    GetMaxHealth: (self: UnitFrame) -> number,
    SetMaxHealth: (self: UnitFrame, maxHealth: number) -> (),
    SetHealthValues: (self: UnitFrame, health: number, maxHealth: number) -> (),
    UpdateHealth: (self: UnitFrame) -> (),
    UpdateHealthFillColor: (self: UnitFrame, unitClass: number) -> (),
    
    GetResource: (self: UnitFrame) -> number,
    SetResource: (self: UnitFrame, resource: number) -> (),
    GetMaxResource: (self: UnitFrame) -> number,
    SetMaxResource: (self: UnitFrame, maxResource: number) -> (),
    SetResourceValues: (self: UnitFrame, resource: number, maxResource: number) -> (),
    UpdateResource: (self: UnitFrame) -> (),
    UpdateResourceFillColor: (self: UnitFrame, resourceType: number) -> (),
}

local unitFrameAPI : UnitFrameAPI = 
{
    unitFrames = {}
};

local LOW_HEALTH_THRESHOLD = 0.25;
local DEFAULT_BORDER_COLOR = vector.create(0.44, 0.46, 0.43);
local SELECTED_BORDER_COLOR = vector.create(0.64, 0.56, 0.18);
local DEAD_BORDER_COLOR = vector.create(0.5, 0.5, 0.5);
local LOW_HEALTH_COLOR = vector.create(0.85, 0.12, 0.10);
local DEFAULT_TEXT_COLOR = vector.create(0.94, 0.94, 0.94);
local HEALTH_COLOR = vector.create(0.42, 0.55, 0.08);
local BAR_FILL_TEXTURE = "texture/interface/unitpowerbaralt/generic2_horizontal_fill.dds";
local BAR_SMOOTH_SPEED = 10.0;

local function GetPercent(value : number, maximum : number) : number
    if maximum <= 0 then
        return 0;
    end

    return math.max(0, math.min(value / maximum, 1));
end

local function FormatNumber(value : number) : string
    local valueString = tostring(math.floor(value));
    local sign = "";

    if string.sub(valueString, 1, 1) == "-" then
        sign = "-";
        valueString = string.sub(valueString, 2);
    end

    local reversed = string.reverse(valueString);
    local grouped = string.gsub(reversed, "(%d%d%d)", "%1,");
    local formatted = string.reverse(grouped);
    if string.sub(formatted, 1, 1) == "," then
        formatted = string.sub(formatted, 2);
    end

    return sign .. formatted;
end

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

    unitFrameTable.SetSelected = function(self : UnitFrame, selected : boolean)
        self.isSelected = selected;
        self:UpdateVisualState();
    end

    unitFrameTable.UpdateVisualState = function(self : UnitFrame)
        local healthPercent = GetPercent(self.health, self.maxHealth);
        local isDead = self.health == 0;
        local isLowHealth = self.maxHealth > 0 and healthPercent <= LOW_HEALTH_THRESHOLD;

        if isDead then
            self.border:SetColor(DEAD_BORDER_COLOR, 1.0);
            self.healthFill:SetColor(LOW_HEALTH_COLOR, 1.0);
            self.healthText:SetColor(LOW_HEALTH_COLOR);
        elseif isLowHealth then
            self.border:SetColor(LOW_HEALTH_COLOR, 1.0);
            self.healthFill:SetColor(LOW_HEALTH_COLOR, 1.0);
            self.healthText:SetColor(LOW_HEALTH_COLOR);
        else
            local borderColor = DEFAULT_BORDER_COLOR;
            if self.isSelected then
                borderColor = SELECTED_BORDER_COLOR;
            end

            self.border:SetColor(borderColor, 1.0);
            self.healthFill:SetColor(self.healthFillColor, 1.0);
            self.healthText:SetColor(DEFAULT_TEXT_COLOR);
        end
    end

    unitFrameTable.UpdateBarFills = function(self : UnitFrame, deltaTime : number, smooth : boolean)
        local healthTarget = GetPercent(self.health, self.maxHealth);
        local resourceTarget = GetPercent(self.resource, self.maxResource);
        local blend = 1.0;

        if smooth then
            blend = 1.0 - math.exp(-BAR_SMOOTH_SPEED * math.max(deltaTime, 0));
        end

        self.displayedHealthPercent += (healthTarget - self.displayedHealthPercent) * blend;
        self.displayedResourcePercent += (resourceTarget - self.displayedResourcePercent) * blend;

        if math.abs(healthTarget - self.displayedHealthPercent) < 0.001 then
            self.displayedHealthPercent = healthTarget;
        end
        if math.abs(resourceTarget - self.displayedResourcePercent) < 0.001 then
            self.displayedResourcePercent = resourceTarget;
        end

        local healthWidth = math.max(0, self.healthBackground:GetWidth() - 2) * self.displayedHealthPercent;
        local resourceWidth = math.max(0, self.resourceBackground:GetWidth() - 2) * self.displayedResourcePercent;
        self.healthFill:SetWidth(healthWidth);
        self.resourceFill:SetWidth(resourceWidth);
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

    unitFrameTable.SetUnitLevel = function(self : UnitFrame, unitLevel : number)
        if self.unitLevel == unitLevel then
            return;
        end

        self.unitLevel = unitLevel;
        self.levelText:SetText(tostring(unitLevel));

        local hasLevel = unitLevel > 0;
        self.levelBackground:SetEnabled(hasLevel);
        self.nameText:SetPosX(if hasLevel then 25 else 5);
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
    unitFrameTable.SetHealthValues = function(self : UnitFrame, health : number, maxHealth : number)
        self.health = health;
        self.maxHealth = maxHealth;

        local text = string.format("%s / %s", FormatNumber(self.health), FormatNumber(self.maxHealth));
        self.healthText:SetText(text);
        self:UpdateVisualState();
    end
    unitFrameTable.UpdateHealth = function(self : UnitFrame)
        local healthPercent = GetPercent(self.health, self.maxHealth);
        self.displayedHealthPercent = healthPercent;
        local newWidth = math.max(0, self.healthBackground:GetWidth() - 2) * healthPercent;
        self.healthFill:SetWidth(newWidth);

        local text = string.format("%s / %s", FormatNumber(self.health), FormatNumber(self.maxHealth));
        self.healthText:SetText(text);
        self:UpdateVisualState();
    end
    unitFrameTable.UpdateHealthFillColor = function(self : UnitFrame, _unitClass : number)
        self.healthFillColor = HEALTH_COLOR;
        self:UpdateVisualState();
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
    unitFrameTable.SetResourceValues = function(self : UnitFrame, resource : number, maxResource : number)
        self.resource = resource;
        self.maxResource = maxResource;

        local text = string.format("%s / %s", FormatNumber(self.resource), FormatNumber(self.maxResource));
        self.resourceText:SetText(text);
    end
    unitFrameTable.UpdateResource = function(self : UnitFrame)
        local resourcePercent = GetPercent(self.resource, self.maxResource);
        self.displayedResourcePercent = resourcePercent;
        local newWidth = math.max(0, self.resourceBackground:GetWidth() - 2) * resourcePercent;
        self.resourceFill:SetWidth(newWidth);

        local text = string.format("%s / %s", FormatNumber(self.resource), FormatNumber(self.maxResource));
        self.resourceText:SetText(text);
    end

    unitFrameTable.UpdateResourceFillColor = function(self : UnitFrame, resourceType : number)
        local color = nil;

        if resourceType == 2 then color = vector.create(0.0, 0.4, 1.0); end -- Mana
        if resourceType == 3 then color = vector.create(1.0, 0.0, 0.0); end -- Rage
        if resourceType == 4 then color = vector.create(1.0, 0.5, 0.25); end -- Focus
        if resourceType == 5 then color = vector.create(1.0, 1.0, 0.0); end -- Energy
        if not color then color = vector.create(0.2, 0.2, 0.2); end -- Default

        self.resourceFill:SetColor(color, 1.0);
    end

    return unitFrameTable;
end

function unitFrameAPI.NewUnitFrame(parent : Widget, posX : number, posY : number, sizeX : number, sizeY : number, layer : number, unitFrameTemplateTable : UnitFrameTemplate)
    local unitFrameTable : UnitFrame = {};

    unitFrameTable.unitID = unitFrameTemplateTable.unitID or -1;
    unitFrameTable.unitName = unitFrameTemplateTable.unitName or "Unknown";
    unitFrameTable.unitLevel = unitFrameTemplateTable.unitLevel or 0;
    unitFrameTable.health = unitFrameTemplateTable.unitHealth or 50;
    unitFrameTable.maxHealth = unitFrameTemplateTable.unitMaxHealth or 100;
    unitFrameTable.resource = unitFrameTemplateTable.unitResource or 75;
    unitFrameTable.maxResource = unitFrameTemplateTable.unitMaxResource or 100;
    unitFrameTable.displayedHealthPercent = GetPercent(unitFrameTable.health, unitFrameTable.maxHealth);
    unitFrameTable.displayedResourcePercent = GetPercent(unitFrameTable.resource, unitFrameTable.maxResource);
    unitFrameTable.isSelected = false;
    unitFrameTable.healthFillColor = HEALTH_COLOR;

    local frameInset = 1;
    local barInset = 4;
    local barGap = 0;
    local headerHeight = 14;

    -- Create the outer frame and its dark inset.
    unitFrameTable.border = parent:NewPanel(posX, posY, sizeX, sizeY, layer, "UnitFrameOuter");
    unitFrameTable.border:SetRelativePoint(0.5, 0.5);
    unitFrameTable.border:SetColor(DEFAULT_BORDER_COLOR, 1.0);

    local backgroundWidth = sizeX - (frameInset * 2);
    -- Keep the inner quad's horizontal edges on distinct pixel rows. With an even
    -- 48px inner height, the renderer's upper-edge coverage consumes the outline.
    local backgroundHeight = sizeY - (frameInset * 2) - 1;

    unitFrameTable.background = unitFrameTable.border:NewPanel(0, -0.5, backgroundWidth, backgroundHeight, 0, "UnitFrameBackground");
    unitFrameTable.background:SetColor(vector.create(0.055, 0.06, 0.055), 0.94);
    unitFrameTable.background:SetAnchor(0.5, 0.5);
    unitFrameTable.background:SetRelativePoint(0.5, 0.5);

    -- Keep identity in a dedicated header above the two status bars.
    unitFrameTable.levelBackground = unitFrameTable.border:NewPanel(frameInset + barInset, -3, 16, 11, 1, "UnitFrameLevelBadge");
    unitFrameTable.levelBackground:SetAnchor(0.0, 1.0);
    unitFrameTable.levelBackground:SetRelativePoint(0.0, 1.0);

    unitFrameTable.levelFill = unitFrameTable.levelBackground:NewPanel(0, 0, 14, 9, 0, "UnitFrameLevelBadgeFill");
    unitFrameTable.levelFill:SetAnchor(0.5, 0.5);
    unitFrameTable.levelFill:SetRelativePoint(0.5, 0.5);

    unitFrameTable.levelText = unitFrameTable.levelFill:NewText(tostring(unitFrameTable.unitLevel), 0, 0, 1, "UnitFrameLevel");
    unitFrameTable.levelText:SetAnchor(0.5, 0.5);
    unitFrameTable.levelText:SetRelativePoint(0.5, 0.5);
    unitFrameTable.levelText:SetPosX(1);

    unitFrameTable.nameText = unitFrameTable.border:NewText(unitFrameTable.unitName, frameInset + barInset + 20, -3, 2, "UnitFrameName");
    unitFrameTable.nameText:SetAnchor(0.0, 1.0);
    unitFrameTable.nameText:SetRelativePoint(0.0, 1.0);

    if unitFrameTable.unitLevel <= 0 then
        unitFrameTable.levelBackground:SetEnabled(false);
        unitFrameTable.nameText:SetPosX(5);
    end

    local barsWidth = backgroundWidth - (barInset * 2);
    local barsHeight = backgroundHeight - headerHeight - barInset;
    local resourceBarHeight = math.max(10, math.floor((barsHeight - barGap) * 0.5));
    local healthBarHeight = math.max(10, barsHeight - resourceBarHeight - barGap);

    unitFrameTable.healthBackground = unitFrameTable.background:NewPanel(barInset, barInset + resourceBarHeight + barGap, barsWidth, healthBarHeight, 0, "UnitFrameBarTrack");
    unitFrameTable.healthBackground:SetColor(vector.create(0.025, 0.028, 0.025), 1.0);
    unitFrameTable.healthBackground:SetRelativePoint(0.0, 0.0);

    unitFrameTable.resourceBackground = unitFrameTable.background:NewPanel(barInset, barInset, barsWidth, resourceBarHeight, 0, "UnitFrameResourceTrack");
    unitFrameTable.resourceBackground:SetRelativePoint(0.0, 0.0);

    -- Create Health Fill
    local fillTextureMinX, fillTextureMinY = UI.PixelsToTexCoord(35, 20, 256, 64);
    local fillTextureMaxX, fillTextureMaxY = UI.PixelsToTexCoord(219, 42, 256, 64);

    unitFrameTable.healthFill = unitFrameTable.healthBackground:NewPanel(1, 1, barsWidth - 2, healthBarHeight - 2, 0, "UnitFrameHealthFill");
    unitFrameTable.healthFill:SetBackground(BAR_FILL_TEXTURE);
    unitFrameTable.healthFill:SetTexCoords(fillTextureMinX, fillTextureMinY, fillTextureMaxX, fillTextureMaxY);
    unitFrameTable.healthFill:SetRelativePoint(0.0, 0.0);
    --unitFrameTable.healthFill:SetClipRect(0, 0, 1, 1);

    -- Create Resource Fill
    unitFrameTable.resourceFill = unitFrameTable.resourceBackground:NewPanel(1, 1, barsWidth - 2, resourceBarHeight - 2, 0, "UnitFrameResourceFill");
    unitFrameTable.resourceFill:SetBackground(BAR_FILL_TEXTURE);
    unitFrameTable.resourceFill:SetTexCoords(fillTextureMinX, fillTextureMinY, fillTextureMaxX, fillTextureMaxY);
    unitFrameTable.resourceFill:SetRelativePoint(0.0, 0.0);
    --unitFrameTable.resourceFill:SetClipRect(0, 0, 1, 1);

    -- Create Health Text
    unitFrameTable.healthText = unitFrameTable.healthBackground:NewText("1000", -4, 0, 1, "UnitFrameHealth");
    unitFrameTable.healthText:SetAnchor(1.0, 0.5);
    unitFrameTable.healthText:SetRelativePoint(1.0, 0.5);

    -- Create Resource Text
    unitFrameTable.resourceText = unitFrameTable.resourceBackground:NewText("1000", -4, 0, 1, "UnitFrameResource");
    unitFrameTable.resourceText:SetAnchor(1.0, 0.5);
    unitFrameTable.resourceText:SetRelativePoint(1.0, 0.5);

    unitFrameTable = SetupUnitFrameMethods(unitFrameTable);
    unitFrameTable:UpdateHealth();
    unitFrameTable:UpdateResource();

    return unitFrameTable;
end

return unitFrameAPI;
