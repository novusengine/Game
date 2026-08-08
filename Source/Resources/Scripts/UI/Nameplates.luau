--!native
local UINameplate = require("@src/API/Game/UI/Nameplate");
local UnitPresentation = require("@src/API/Game/UI/UnitPresentation");
local OptionsContext = require("@src/API/OptionsContext")

local nameplates = 
{
    entries = {},
    deadByUnitID = {},
    localMoverUnitID = nil,
};

local IS_STANDALONE_MODE = true

local LOCAL_PLAYER_NAME_COLOR = vec.New(96.0 / 255.0, 96.0 / 255.0, 255.0 / 255.0);
local HOSTILE_UNIT_NAME_COLOR = vec.New(1.0, 0.0, 0.0);
local UNFRIENDLY_UNIT_NAME_COLOR = vec.New(0.95, 0.48, 0.18);
local NEUTRAL_UNIT_NAME_COLOR = vec.New(1.0, 1.0, 0.0);
local FRIENDLY_UNIT_NAME_COLOR = vec.New(0.0, 1.0, 0.0);
local NAME_PALETTE = {
    hostile = HOSTILE_UNIT_NAME_COLOR,
    unfriendly = UNFRIENDLY_UNIT_NAME_COLOR,
    neutral = NEUTRAL_UNIT_NAME_COLOR,
    friendly = FRIENDLY_UNIT_NAME_COLOR,
};
local DEAD_UNIT_NAME_COLOR = vec.New(0.5, 0.5, 0.5);

local function GetUnitNameColor(unitID : integer)
    if Unit.GetLocal() == unitID then
        return LOCAL_PLAYER_NAME_COLOR;
    end

    return UnitPresentation.GetReactionColor(Unit.GetReaction(unitID), NAME_PALETTE);
end

local function RefreshUnitNameColor(unitID : integer, nameplate : any, force : boolean?)
    local health = Unit.GetHealth(unitID);
    local isDead = health == 0;
    if not force and nameplates.deadByUnitID[unitID] == isDead then
        return;
    end

    nameplates.deadByUnitID[unitID] = isDead;
    nameplate:SetNameColor(if isDead then DEAD_UNIT_NAME_COLOR else GetUnitNameColor(unitID));
end

local function RegisterTemplates()
    UI.RegisterPanelTemplate("NameplateHealthBackground",
    {
        cornerRadius = 1.5,
        color = vec.New(0.035, 0.04, 0.035),
    });

    UI.RegisterPanelTemplate("NameplateHealthFill",
    {
        cornerRadius = 1.0,
        color = vec.New(0.32, 0.62, 0.07),
    });

    UI.RegisterTextTemplate("NameplateName",
    {
        font = "Data/Fonts/QTFrizQuad.otf", -- Obviously we want a different font
        size = 13.0,
        color = NEUTRAL_UNIT_NAME_COLOR,
        borderSize = 0.0,
        borderColor = vec.New(0.0, 0.0, 0.0),
    });

    UI.RegisterTextTemplate("NameplateHealth",
    {
        font = "Data/Fonts/OpenSans-Regular.ttf", -- Obviously we want a different font
        size = 9.0,
        color = vec.New(210.0/255.0, 210.0/255.0, 210.0/255.0),
        borderSize = 0.0,
        borderColor = vec.New(0.0, 0.0, 0.0),
    });
end

local function SetEnableNameplates(enabled : boolean)
    for i, nameplate in nameplates.entries do
        nameplate:SetEnabled(enabled);
    end
end

-- Setup options
local optionCategory = OptionsContext:GetCategory(OptionsContext.OptionCategoryType.Gameplay);
local optionGroup = optionCategory:Get("Nameplates");
local optionSection = optionGroup:Get("General");

-- Enabled option
local enabledOption = optionSection:RegisterOption("Enabled", true, OptionsContext.OptionType.Checkbox);
local enabledCancelCallback = enabledOption:RegisterOnValueChanged(function(self, value)
    SetEnableNameplates(value);
    return true;
end);
--enabledCancelCallback(); -- Call this to cancel the callback

local function OnGameLoaded(eventID : integer, data : any)
    RegisterTemplates();

    -- Ensure Nameplates Canvas is created
    UI.GetCanvas("Nameplates", vec.New(0, 0), vec.New(1920, 1080));
end

local function OnGameUpdated(eventID : integer, data : any)
    local enabled = enabledOption:Get();
    if (not enabled) then
        return;
    end

    for i, nameplate in nameplates.entries do
        local unitID = nameplate:GetUnitID();
        local health, maxHealth = Unit.GetHealth(unitID);
        nameplate:SetHealthValues(math.ceil(health), math.ceil(maxHealth));
        RefreshUnitNameColor(unitID, nameplate);
        nameplate:SetPosToUnit(unitID);
    end
end

local function OnUnitAdd(eventID : integer, data : any)
    local canvas : Canvas = UI.GetCanvas("Nameplates", vec.New(0, 0), vec.New(1920, 1080));

    local existingNameplate = nameplates.entries[data.unitID];
    if existingNameplate ~= nil then
        existingNameplate:Destroy();
    end

    local localMoverUnitID = Unit.GetLocal();
    local nameplate = UINameplate.New(canvas, vec.New(0, 0), vec.New(125, 20), 0i, {
        unitID = data.unitID,
        unitName = Unit.GetName(data.unitID),
        isStandalone = IS_STANDALONE_MODE or data.unitID == localMoverUnitID,
    });

    local health, maxHealth = Unit.GetHealth(data.unitID);
    nameplate:SetHealthValues(math.ceil(health), math.ceil(maxHealth));
    RefreshUnitNameColor(data.unitID, nameplate, true);
    nameplates.entries[data.unitID] = nameplate;
    nameplate:SetPosToUnit(data.unitID);
end

local function OnUnitRemove(eventID : integer, data : any)
    local nameplate = nameplates.entries[data.unitID];
    if (nameplate == nil) then
        return
    end

    nameplate:Destroy();
    nameplates.entries[data.unitID] = nil;
    nameplates.deadByUnitID[data.unitID] = nil;
end

local function OnUnitReactionChanged(eventID : integer, data : any)
    local nameplate = nameplates.entries[data.unitID];
    if nameplate ~= nil then
        RefreshUnitNameColor(data.unitID, nameplate, true);
    end
end

local function OnLocalMoverChanged(eventID : integer, data : any)
    local previousMoverUnitID = nameplates.localMoverUnitID;
    nameplates.localMoverUnitID = data.moverID;

    if previousMoverUnitID ~= nil and previousMoverUnitID ~= data.moverID then
        local previousNameplate = nameplates.entries[previousMoverUnitID];
        if previousNameplate ~= nil then
            previousNameplate:SetStandalone(false);
            RefreshUnitNameColor(previousMoverUnitID, previousNameplate, true);
        end
    end

    local localNameplate = nameplates.entries[data.moverID];
    if localNameplate ~= nil then
        localNameplate:SetStandalone(true);
        RefreshUnitNameColor(data.moverID, localNameplate, true);
    end
end

RegisterEvent(GameEvent.Loaded, OnGameLoaded);
RegisterEvent(GameEvent.Updated, OnGameUpdated);
RegisterEvent(UnitEvent.Add, OnUnitAdd);
RegisterEvent(UnitEvent.Remove, OnUnitRemove);
RegisterEvent(UnitEvent.ReactionChanged, OnUnitReactionChanged);
RegisterEvent(GameEvent.LocalMoverChanged, OnLocalMoverChanged);
