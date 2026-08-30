local OptionsContext = require("@src/API/OptionsContext")
local UnitPresentation = require("@src/API/Game/UI/UnitPresentation")
local RootedWidget = require("@src/API/UI/RootedWidget")

export type UnitFrameAPI = 
{
    New : (parent : Widget, position : vec, size : vec, layer : integer, unitFrameTemplateTable : UnitFrameTemplate) -> UnitFrame,

    unitFrames : { UnitFrame }
}

export type UnitFrameTemplate =
{
    unitID : integer,
    unitName : string,
    unitLevel : number?,
    unitHealth : number?,
    unitMaxHealth : number?,
    unitResource : number?,
    unitMaxResource : number?
}

export type UnitFrame = {
    -- Fields
    root: Panel,
    unitID: integer,
    unitName: string,
    unitLevel: number,

    health: number,
    maxHealth: number,
    resource: number,
    maxResource: number,
    displayedHealthPercent: number,
    displayedResourcePercent: number,
    isDamageTrailActive: boolean,
    damageTrailPercent: number,
    damageTrailDelay: number,
    isHealingTrailActive: boolean,
    healingTrailPercent: number,
    healingTrailDelay: number,
    reaction: integer,
    playerLevel: number,
    isTargetingPlayer: boolean,
    maxNameWidth: number,

    border: Panel,
    background: Panel,
    headerRule: Panel,
    levelBackground: Panel,
    levelFill: Panel,
    healthBackground: Panel,
    resourceBackground: Panel,
    resourceTrack: Panel,
    healthDamageTrail: Panel,
    healthFill: Panel,
    healthHealingTrail: Panel,
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

    SetUnitReaction: (self: UnitFrame, reaction: integer, playerLevel: number) -> (),
    SetTargetingPlayer: (self: UnitFrame, isTargetingPlayer: boolean) -> (),
    UpdateVisualState: (self: UnitFrame) -> (),
    UpdateBarFills: (self: UnitFrame, deltaTime: number, smooth: boolean) -> (),
    
    SetAnchor: (self: UnitFrame, anchor: vec) -> (),
    SetRelativePoint: (self: UnitFrame, point: vec) -> (),

    GetUnitID: (self: UnitFrame) -> integer,
    SetUnitID: (self: UnitFrame, unitID: integer) -> (),
    GetUnitName: (self: UnitFrame) -> string,
    SetUnitName: (self: UnitFrame, unitName: string) -> (),
    SetUnitLevel: (self: UnitFrame, unitLevel: number, playerLevel: number?) -> (),
    
    GetHealth: (self: UnitFrame) -> number,
    SetHealth: (self: UnitFrame, health: number) -> (),
    GetMaxHealth: (self: UnitFrame) -> number,
    SetMaxHealth: (self: UnitFrame, maxHealth: number) -> (),
    SetHealthValues: (self: UnitFrame, health: number, maxHealth: number) -> (),
    UpdateHealth: (self: UnitFrame) -> (),

    GetResource: (self: UnitFrame) -> number,
    SetResource: (self: UnitFrame, resource: number) -> (),
    GetMaxResource: (self: UnitFrame) -> number,
    SetMaxResource: (self: UnitFrame, maxResource: number) -> (),
    SetResourceValues: (self: UnitFrame, resource: number, maxResource: number) -> (),
    UpdateResource: (self: UnitFrame) -> (),
    UpdateResourceFillColor: (self: UnitFrame, resourceType: integer) -> (),
}

local unitFrameAPI : UnitFrameAPI = 
{
    unitFrames = {}
};

local DEFAULT_BORDER_COLOR = vec.New(0.44, 0.46, 0.43);
local HOSTILE_TARGET_BORDER_COLOR = vec.New(0.82, 0.12, 0.10);
local UNFRIENDLY_TARGET_BORDER_COLOR = vec.New(0.82, 0.32, 0.10);
local DEAD_BORDER_COLOR = vec.New(0.26, 0.27, 0.25);

local DEFAULT_TEXT_COLOR = vec.New(0.94, 0.94, 0.94);
local HOSTILE_NAME_COLOR = vec.New(1.0, 0.0, 0.0);
local UNFRIENDLY_NAME_COLOR = vec.New(0.95, 0.48, 0.18);
local NEUTRAL_NAME_COLOR = vec.New(1.0, 1.0, 0.0);
local FRIENDLY_NAME_COLOR = vec.New(0.0, 1.0, 0.0);
local NAME_PALETTE = {
    hostile = HOSTILE_NAME_COLOR,
    unfriendly = UNFRIENDLY_NAME_COLOR,
    neutral = NEUTRAL_NAME_COLOR,
    friendly = FRIENDLY_NAME_COLOR,
};
local DEAD_IDENTITY_COLOR = vec.New(0.50, 0.50, 0.48);

local TRIVIAL_LEVEL_COLOR = vec.New(0.52, 0.54, 0.52);
local EASY_LEVEL_COLOR = vec.New(0.35, 0.74, 0.40);
local EVEN_LEVEL_COLOR = vec.New(1.0, 1.0, 0.0);
local DIFFICULT_LEVEL_COLOR = vec.New(0.95, 0.55, 0.18);
local EXTREME_LEVEL_COLOR = vec.New(0.92, 0.20, 0.18);

local HEADER_RULE_ALPHA = 0.70;
local BAR_SMOOTH_SPEED = 10.0;
local DAMAGE_TRAIL_HOLD_TIME = 0.30;
local DAMAGE_TRAIL_CATCHUP_SPEED = 6.0;
local HEALING_TRAIL_HOLD_TIME = 0.10;
local HEALING_TRAIL_CATCHUP_SPEED = 8.0;

local function GetPercent(value : number, maximum : number) : number
    if maximum <= 0 then
        return 0;
    end

    return math.max(0, math.min(value / maximum, 1));
end

local function GetNameColor(reaction : integer) : vec
    return UnitPresentation.GetReactionColor(reaction, NAME_PALETTE);
end

local function GetLevelPresentation(unitLevel : number, playerLevel : number, reaction : integer) : (string, vec)
    if reaction == FactionReaction.Friendly then
        return tostring(unitLevel), EVEN_LEVEL_COLOR;
    end

    local levelDelta = unitLevel - playerLevel;
    if levelDelta <= -6 then
        return tostring(unitLevel), TRIVIAL_LEVEL_COLOR;
    elseif levelDelta <= -2 then
        return tostring(unitLevel), EASY_LEVEL_COLOR;
    elseif levelDelta <= 2 then
        return tostring(unitLevel), EVEN_LEVEL_COLOR;
    elseif levelDelta <= 4 then
        return tostring(unitLevel), DIFFICULT_LEVEL_COLOR;
    elseif levelDelta <= 9 then
        return tostring(unitLevel), EXTREME_LEVEL_COLOR;
    end

    if reaction == FactionReaction.Neutral then
        return tostring(unitLevel), EXTREME_LEVEL_COLOR;
    end

    return "?", EXTREME_LEVEL_COLOR;
end

local function RefreshIdentityVisualState(unitFrame : UnitFrame)
    local levelDisplay, levelColor = GetLevelPresentation(unitFrame.unitLevel, unitFrame.playerLevel, unitFrame.reaction);
    local isDead = unitFrame.health == 0;

    unitFrame.nameText:SetColor(if isDead then DEAD_IDENTITY_COLOR else GetNameColor(unitFrame.reaction));
    unitFrame.levelText:SetColor(if isDead then DEAD_IDENTITY_COLOR else levelColor);
    unitFrame.levelText:SetText(levelDisplay);
    unitFrame.levelBackground:SetEnabled(unitFrame.unitLevel > 0);
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

local function GetAsciiUnitName(unitName : string) : string
    local characters : { string } = {};
    local isReplacingNonAscii = false;

    for index = 1, #unitName do
        local byte = string.byte(unitName, index);
        local isPrintableAscii = byte >= 32 and byte <= 126;

        if isPrintableAscii then
            table.insert(characters, string.char(byte));
            isReplacingNonAscii = false;
        elseif not isReplacingNonAscii then
            table.insert(characters, "?");
            isReplacingNonAscii = true;
        end
    end

    local asciiName = table.concat(characters);
    return if asciiName == "" then "Unknown" else asciiName;
end

local function SetFittedName(nameText : TextWidget, unitName : string, maxWidth : number)
    nameText:SetText(unitName);
    if nameText:GetWidth() <= maxWidth then
        return;
    end

    local characterCount = #unitName;
    while characterCount > 0 do
        characterCount -= 1;
        nameText:SetText(string.sub(unitName, 1, characterCount) .. "...");

        if nameText:GetWidth() <= maxWidth then
            return;
        end
    end
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

    unitFrameTable.SetUnitReaction = function(self : UnitFrame, reaction : integer, playerLevel : number)
        if self.reaction == reaction and self.playerLevel == playerLevel then
            return;
        end

        self.reaction = reaction;
        self.playerLevel = playerLevel;
        self:UpdateVisualState();
    end

    unitFrameTable.SetTargetingPlayer = function(self : UnitFrame, isTargetingPlayer : boolean)
        if self.isTargetingPlayer == isTargetingPlayer then
            return;
        end

        self.isTargetingPlayer = isTargetingPlayer;
        self:UpdateVisualState();
    end

    unitFrameTable.UpdateVisualState = function(self : UnitFrame)
        local healthPercent = GetPercent(self.health, self.maxHealth);
        local isDead = self.health == 0;
        local borderColor = DEFAULT_BORDER_COLOR;

        if isDead then
            borderColor = DEAD_BORDER_COLOR;
        elseif self.isTargetingPlayer then
            if self.reaction == FactionReaction.Hostile then
                borderColor = HOSTILE_TARGET_BORDER_COLOR;
            elseif self.reaction == FactionReaction.Unfriendly then
                borderColor = UNFRIENDLY_TARGET_BORDER_COLOR;
            end
        end

        self.border:SetColor(borderColor, 1.0);
        self.headerRule:SetColor(borderColor, HEADER_RULE_ALPHA);
        self.healthFill:SetColor(UnitPresentation.GetHealthFillColor(healthPercent), 1.0);
        self.healthText:SetColor(if isDead then DEAD_IDENTITY_COLOR else DEFAULT_TEXT_COLOR);
        RefreshIdentityVisualState(self);
    end

    unitFrameTable.UpdateBarFills = function(self : UnitFrame, deltaTime : number, smooth : boolean)
        local healthTarget = GetPercent(self.health, self.maxHealth);
        local resourceTarget = GetPercent(self.resource, self.maxResource);
        local blend = 1.0;

        if smooth then
            blend = 1.0 - math.exp(-BAR_SMOOTH_SPEED * math.max(deltaTime, 0));
        end

        local healthBlend = blend;
        if self.isHealingTrailActive and deltaTime > 0 then
            if self.healingTrailDelay > 0 then
                healthBlend = 0;
            else
                healthBlend = 1.0 - math.exp(-HEALING_TRAIL_CATCHUP_SPEED * deltaTime);
            end
        end

        self.displayedHealthPercent += (healthTarget - self.displayedHealthPercent) * healthBlend;
        self.displayedResourcePercent += (resourceTarget - self.displayedResourcePercent) * blend;

        if math.abs(healthTarget - self.displayedHealthPercent) < 0.001 then
            self.displayedHealthPercent = healthTarget;
        end
        if math.abs(resourceTarget - self.displayedResourcePercent) < 0.001 then
            self.displayedResourcePercent = resourceTarget;
        end

        local healthInnerWidth = math.max(0, self.healthBackground:GetWidth() - 2);
        local healthWidth = healthInnerWidth * self.displayedHealthPercent;
        local resourceWidth = self.resourceTrack:GetWidth() * self.displayedResourcePercent;
        self.healthFill:SetWidth(healthWidth);
        self.resourceFill:SetWidth(resourceWidth);

        if deltaTime <= 0 then
            self.isDamageTrailActive = false;
            self.damageTrailPercent = healthTarget;
            self.damageTrailDelay = 0;
            self.isHealingTrailActive = false;
            self.healingTrailPercent = healthTarget;
            self.healingTrailDelay = 0;
        else
            if self.isDamageTrailActive then
                self.damageTrailDelay = math.max(0, self.damageTrailDelay - deltaTime);
                if self.damageTrailDelay == 0 then
                    local trailBlend = 1.0 - math.exp(-DAMAGE_TRAIL_CATCHUP_SPEED * deltaTime);
                    self.damageTrailPercent += (self.displayedHealthPercent - self.damageTrailPercent) * trailBlend;
                end

                self.damageTrailPercent = math.max(self.damageTrailPercent, self.displayedHealthPercent);
            else
                self.damageTrailPercent = self.displayedHealthPercent;
            end

            if self.isHealingTrailActive then
                self.healingTrailDelay = math.max(0, self.healingTrailDelay - deltaTime);
                self.healingTrailPercent = math.max(self.healingTrailPercent, self.displayedHealthPercent);
            else
                self.healingTrailPercent = self.displayedHealthPercent;
                self.healingTrailDelay = 0;
            end
        end

        local damageTrailWidth = healthInnerWidth * self.damageTrailPercent;
        local isDamageTrailVisible = damageTrailWidth - healthWidth >= 0.5;
        self.healthDamageTrail:SetWidth(damageTrailWidth);
        self.healthDamageTrail:SetEnabled(self.isDamageTrailActive and isDamageTrailVisible);

        if self.isDamageTrailActive and self.damageTrailDelay == 0 and not isDamageTrailVisible then
            self.isDamageTrailActive = false;
        end

        local healingTrailWidth = healthInnerWidth * self.healingTrailPercent;
        local isHealingTrailVisible = healingTrailWidth - healthWidth >= 0.5;
        self.healthHealingTrail:SetWidth(healingTrailWidth);
        self.healthHealingTrail:SetEnabled(self.isHealingTrailActive and isHealingTrailVisible);

        if self.isHealingTrailActive and not isHealingTrailVisible then
            self.isHealingTrailActive = false;
        end
    end

    unitFrameTable.SetAnchor = function(self : UnitFrame, anchor : vec)
        self.border:SetAnchor(anchor);
    end

    unitFrameTable.SetRelativePoint = function(self : UnitFrame, point : vec)
        self.border:SetRelativePoint(point);
    end

    unitFrameTable.GetUnitID = function(self : UnitFrame)
        return self.unitID;
    end

    unitFrameTable.SetUnitID = function(self : UnitFrame, unitID : integer)
        self.unitID = unitID;
    end

    unitFrameTable.GetUnitName = function(self : UnitFrame)
        return self.unitName;
    end

    unitFrameTable.SetUnitName = function(self : UnitFrame, unitName : string)
        self.unitName = GetAsciiUnitName(unitName);
        SetFittedName(self.nameText, self.unitName, self.maxNameWidth);
    end

    unitFrameTable.SetUnitLevel = function(self : UnitFrame, unitLevel : number, playerLevel : number?)
        local newPlayerLevel = playerLevel or self.playerLevel;
        if self.unitLevel == unitLevel and self.playerLevel == newPlayerLevel then
            return;
        end

        self.unitLevel = unitLevel;
        self.playerLevel = newPlayerLevel;
        RefreshIdentityVisualState(self);
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
        local previousHealth = self.health;
        local previousHealthPercent = GetPercent(self.health, self.maxHealth);
        local newHealthPercent = GetPercent(health, maxHealth);

        if health < previousHealth then
            local damageTrailPercent = math.max(self.displayedHealthPercent, previousHealthPercent);
            if self.isDamageTrailActive then
                damageTrailPercent = math.max(damageTrailPercent, self.damageTrailPercent);
            end

            self.isDamageTrailActive = true;
            self.damageTrailPercent = damageTrailPercent;
            self.damageTrailDelay = DAMAGE_TRAIL_HOLD_TIME;
            self.isHealingTrailActive = false;
            self.healingTrailDelay = 0;
        elseif health > previousHealth and newHealthPercent > previousHealthPercent then
            local healingTrailPercent = newHealthPercent;
            if self.isHealingTrailActive then
                healingTrailPercent = math.max(healingTrailPercent, self.healingTrailPercent);
            end

            self.isDamageTrailActive = false;
            self.damageTrailDelay = 0;
            self.isHealingTrailActive = true;
            self.healingTrailPercent = healingTrailPercent;
            self.healingTrailDelay = HEALING_TRAIL_HOLD_TIME;
        elseif maxHealth ~= self.maxHealth then
            self.isDamageTrailActive = false;
            self.damageTrailDelay = 0;
            self.isHealingTrailActive = false;
            self.healingTrailDelay = 0;
        end

        self.health = health;
        self.maxHealth = maxHealth;

        local text = string.format("%s / %s", FormatNumber(self.health), FormatNumber(self.maxHealth));
        self.healthText:SetText(text);
        self:UpdateVisualState();
    end
    unitFrameTable.UpdateHealth = function(self : UnitFrame)
        local healthPercent = GetPercent(self.health, self.maxHealth);
        self.displayedHealthPercent = healthPercent;
        self.isDamageTrailActive = false;
        self.damageTrailPercent = healthPercent;
        self.damageTrailDelay = 0;
        self.isHealingTrailActive = false;
        self.healingTrailPercent = healthPercent;
        self.healingTrailDelay = 0;
        self.healthDamageTrail:SetEnabled(false);
        self.healthHealingTrail:SetEnabled(false);

        local newWidth = math.max(0, self.healthBackground:GetWidth() - 2) * healthPercent;
        self.healthFill:SetWidth(newWidth);

        local text = string.format("%s / %s", FormatNumber(self.health), FormatNumber(self.maxHealth));
        self.healthText:SetText(text);
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
        local newWidth = self.resourceTrack:GetWidth() * resourcePercent;
        self.resourceFill:SetWidth(newWidth);

        local text = string.format("%s / %s", FormatNumber(self.resource), FormatNumber(self.maxResource));
        self.resourceText:SetText(text);
    end

    unitFrameTable.UpdateResourceFillColor = function(self : UnitFrame, resourceType : integer)
        local fillColor = vec.New(0.16, 0.16, 0.16);
        local trackColor = vec.New(0.075, 0.08, 0.075);

        if resourceType == 2i then -- Mana
            fillColor = vec.New(0.0, 0.29, 0.72);
            trackColor = vec.New(0.05, 0.125, 0.21);
        elseif resourceType == 3i then -- Rage
            fillColor = vec.New(0.72, 0.035, 0.03);
            trackColor = vec.New(0.22, 0.05, 0.04);
        elseif resourceType == 4i then -- Focus
            fillColor = vec.New(0.75, 0.35, 0.16);
            trackColor = vec.New(0.22, 0.105, 0.045);
        elseif resourceType == 5i then -- Energy
            fillColor = vec.New(0.75, 0.69, 0.06);
            trackColor = vec.New(0.20, 0.175, 0.04);
        end

        self.resourceTrack:SetColor(trackColor, 1.0);
        self.resourceFill:SetColor(fillColor, 1.0);
    end

    return unitFrameTable;
end

function unitFrameAPI.New(parent : Widget, position : vec, size : vec, layer : integer, unitFrameTemplateTable : UnitFrameTemplate)
    local unitFrameTable : UnitFrame = {} :: any;

    unitFrameTable.unitID = unitFrameTemplateTable.unitID or -1i;
    unitFrameTable.unitName = GetAsciiUnitName(unitFrameTemplateTable.unitName or "Unknown");
    unitFrameTable.unitLevel = unitFrameTemplateTable.unitLevel or 0;
    unitFrameTable.health = unitFrameTemplateTable.unitHealth or 50;
    unitFrameTable.maxHealth = unitFrameTemplateTable.unitMaxHealth or 100;
    unitFrameTable.resource = unitFrameTemplateTable.unitResource or 75;
    unitFrameTable.maxResource = unitFrameTemplateTable.unitMaxResource or 100;
    unitFrameTable.displayedHealthPercent = GetPercent(unitFrameTable.health, unitFrameTable.maxHealth);
    unitFrameTable.displayedResourcePercent = GetPercent(unitFrameTable.resource, unitFrameTable.maxResource);
    unitFrameTable.isDamageTrailActive = false;
    unitFrameTable.damageTrailPercent = unitFrameTable.displayedHealthPercent;
    unitFrameTable.damageTrailDelay = 0;
    unitFrameTable.isHealingTrailActive = false;
    unitFrameTable.healingTrailPercent = unitFrameTable.displayedHealthPercent;
    unitFrameTable.healingTrailDelay = 0;
    unitFrameTable.reaction = FactionReaction.Friendly;
    unitFrameTable.playerLevel = 0;
    unitFrameTable.isTargetingPlayer = false;

    local frameInset = 1;
    local barInset = 2;
    local barOverlap = 1;
    local headerHeight = 14;
    local levelBadgeWidth = 15;
    local nameBadgeGap = 3;

    -- Create the outer frame and its dark inset.
    unitFrameTable.border = parent:NewPanel(position, size, layer, "UnitFrameOuter");
    unitFrameTable.root = unitFrameTable.border;
    RootedWidget.Attach(unitFrameTable);
    unitFrameTable.border:SetRelativePoint(vec.New(0.5, 0.5));
    unitFrameTable.border:SetColor(DEFAULT_BORDER_COLOR, 1.0);

    local backgroundWidth = size.x - (frameInset * 2);
    -- Keep the inner quad's horizontal edges on distinct pixel rows. With an even
    -- 48px inner height, the renderer's upper-edge coverage consumes the outline.
    local backgroundHeight = size.y - (frameInset * 2) - 1;

    unitFrameTable.background = unitFrameTable.border:NewPanel(vec.New(0, -0.5), vec.New(backgroundWidth, backgroundHeight), 0i, "UnitFrameBackground");
    unitFrameTable.background:SetColor(vec.New(0.065, 0.07, 0.064), 1.0);
    unitFrameTable.background:SetAnchor(vec.New(0.5, 0.5));
    unitFrameTable.background:SetRelativePoint(vec.New(0.5, 0.5));

    -- Keep identity in a dedicated header above the two status bars.
    local headerRuleWidth = size.x - ((frameInset + barInset) * 2) - levelBadgeWidth - nameBadgeGap;
    unitFrameTable.headerRule = unitFrameTable.border:NewPanel(vec.New(frameInset + barInset, -headerHeight), vec.New(headerRuleWidth, 1), 1i, "UnitFrameHeaderRule");
    unitFrameTable.headerRule:SetAnchor(vec.New(0.0, 1.0));
    unitFrameTable.headerRule:SetRelativePoint(vec.New(0.0, 1.0));
    unitFrameTable.headerRule:SetColor(DEFAULT_BORDER_COLOR, HEADER_RULE_ALPHA);

    unitFrameTable.levelBackground = unitFrameTable.border:NewPanel(vec.New(-(frameInset + barInset), -3), vec.New(levelBadgeWidth, 11), 1i, "UnitFrameLevelBadge");
    unitFrameTable.levelBackground:SetAnchor(vec.New(1.0, 1.0));
    unitFrameTable.levelBackground:SetRelativePoint(vec.New(1.0, 1.0));

    unitFrameTable.levelFill = unitFrameTable.levelBackground:NewPanel(vec.New(0, 0), vec.New(13, 9), 0i, "UnitFrameLevelBadgeFill");
    unitFrameTable.levelFill:SetAnchor(vec.New(0.5, 0.5));
    unitFrameTable.levelFill:SetRelativePoint(vec.New(0.5, 0.5));

    unitFrameTable.levelText = unitFrameTable.levelFill:NewText(tostring(unitFrameTable.unitLevel), vec.New(0, 0), 1i, "UnitFrameLevel");
    unitFrameTable.levelText:SetAnchor(vec.New(0.5, 0.5));
    unitFrameTable.levelText:SetRelativePoint(vec.New(0.5, 0.5));
    unitFrameTable.levelText:SetPosX(1.5);

    unitFrameTable.nameText = unitFrameTable.border:NewText(unitFrameTable.unitName, vec.New(frameInset + barInset + 1, -3), 2i, "UnitFrameName");
    unitFrameTable.nameText:SetAnchor(vec.New(0.0, 1.0));
    unitFrameTable.nameText:SetRelativePoint(vec.New(0.0, 1.0));
    unitFrameTable.maxNameWidth = headerRuleWidth - 1;
    SetFittedName(unitFrameTable.nameText, unitFrameTable.unitName, unitFrameTable.maxNameWidth);

    if unitFrameTable.unitLevel <= 0 then
        unitFrameTable.levelBackground:SetEnabled(false);
    end

    local barsWidth = backgroundWidth - (barInset * 2);
    local barsHeight = backgroundHeight - headerHeight - barInset;
    local resourceBarHeight = math.max(10, math.floor((barsHeight + barOverlap) * 0.5));
    local healthBarHeight = math.max(10, barsHeight - resourceBarHeight + barOverlap);

    unitFrameTable.healthBackground = unitFrameTable.background:NewPanel(vec.New(barInset, barInset + resourceBarHeight - barOverlap), vec.New(barsWidth, healthBarHeight), 0i, "UnitFrameBarTrack");
    unitFrameTable.healthBackground:SetColor(vec.New(0.035, 0.04, 0.035), 1.0);
    unitFrameTable.healthBackground:SetRelativePoint(vec.New(0.0, 0.0));

    unitFrameTable.resourceBackground = unitFrameTable.background:NewPanel(vec.New(barInset, barInset), vec.New(barsWidth, resourceBarHeight), 0i, "UnitFrameResourceBorder");
    unitFrameTable.resourceBackground:SetRelativePoint(vec.New(0.0, 0.0));

    unitFrameTable.resourceTrack = unitFrameTable.resourceBackground:NewPanel(vec.New(1, 1), vec.New(barsWidth - 2, resourceBarHeight - 2), 0i, "UnitFrameResourceTrack");
    unitFrameTable.resourceTrack:SetRelativePoint(vec.New(0.0, 0.0));

    -- Keep recent damage behind the current health fill.
    unitFrameTable.healthDamageTrail = unitFrameTable.healthBackground:NewPanel(vec.New(1, 1), vec.New(barsWidth - 2, healthBarHeight - 2), 0i, "UnitFrameHealthDamageTrail");
    unitFrameTable.healthDamageTrail:SetRelativePoint(vec.New(0.0, 0.0));
    unitFrameTable.healthDamageTrail:SetEnabled(false);

    -- Keep restored health ahead of the current fill until the fill catches up.
    unitFrameTable.healthHealingTrail = unitFrameTable.healthBackground:NewPanel(vec.New(1, 1), vec.New(barsWidth - 2, healthBarHeight - 2), 0i, "UnitFrameHealthHealingTrail");
    unitFrameTable.healthHealingTrail:SetRelativePoint(vec.New(0.0, 0.0));
    unitFrameTable.healthHealingTrail:SetAlpha(0.75);
    unitFrameTable.healthHealingTrail:SetEnabled(false);

    -- Create Health Fill
    unitFrameTable.healthFill = unitFrameTable.healthBackground:NewPanel(vec.New(1, 1), vec.New(barsWidth - 2, healthBarHeight - 2), 0i, "UnitFrameHealthFill");
    unitFrameTable.healthFill:SetRelativePoint(vec.New(0.0, 0.0));
    --unitFrameTable.healthFill:SetClipRect(vec.New(0, 0), vec.New(1, 1));

    -- Create Resource Fill
    unitFrameTable.resourceFill = unitFrameTable.resourceTrack:NewPanel(vec.New(0, 0), vec.New(barsWidth - 2, resourceBarHeight - 2), 0i, "UnitFrameResourceFill");
    unitFrameTable.resourceFill:SetRelativePoint(vec.New(0.0, 0.0));
    --unitFrameTable.resourceFill:SetClipRect(vec.New(0, 0), vec.New(1, 1));

    -- Create Health Text
    unitFrameTable.healthText = unitFrameTable.healthBackground:NewText("1000", vec.New(0, 0), 1i, "UnitFrameHealth");
    unitFrameTable.healthText:SetAnchor(vec.New(0.5, 0.5));
    unitFrameTable.healthText:SetRelativePoint(vec.New(0.5, 0.5));
    unitFrameTable.healthText:SetPosX(0.5);

    -- Create Resource Text
    unitFrameTable.resourceText = unitFrameTable.resourceBackground:NewText("1000", vec.New(0, 0), 1i, "UnitFrameResource");
    unitFrameTable.resourceText:SetAnchor(vec.New(0.5, 0.5));
    unitFrameTable.resourceText:SetRelativePoint(vec.New(0.5, 0.5));
    unitFrameTable.resourceText:SetPosX(0.5);

    unitFrameTable = SetupUnitFrameMethods(unitFrameTable);
    unitFrameTable:UpdateHealth();
    unitFrameTable:UpdateResource();

    return unitFrameTable;
end

return unitFrameAPI;
