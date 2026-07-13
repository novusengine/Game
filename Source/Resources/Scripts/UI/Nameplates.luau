--!native
local UINameplate = require("@src/API/UI/Nameplate");
local OptionsContext = require("@src/API/OptionsContext")

local nameplates = 
{
    entries = {},
    deadByUnitID = {},
    localTargetUnitID = nil,
    localMoverUnitID = nil,
};

local LOCAL_PLAYER_NAME_COLOR = vector.create(0.22, 0.58, 0.88);
local NEUTRAL_UNIT_NAME_COLOR = vector.create(0.88, 0.76, 0.32);
local DEAD_UNIT_NAME_COLOR = vector.create(0.5, 0.5, 0.5);

local function GetUnitNameColor(unitID : number)
    local localMoverUnitID = nameplates.localMoverUnitID or Unit.GetLocal();
    return if localMoverUnitID == unitID then LOCAL_PLAYER_NAME_COLOR else NEUTRAL_UNIT_NAME_COLOR;
end

local function RefreshUnitNameColor(unitID : number, nameplate : any, force : boolean?)
    local health = Unit.GetHealth(unitID);
    local isDead = health == 0;
    if not force and nameplates.deadByUnitID[unitID] == isDead then
        return;
    end

    nameplates.deadByUnitID[unitID] = isDead;
    nameplate:SetNameColor(if isDead then DEAD_UNIT_NAME_COLOR else GetUnitNameColor(unitID));
end

local function RegisterTemplates()
    UI.RegisterTextTemplate("PlayerName",
    {
        font = "Data/Fonts/OpenSans-Medium.ttf", -- Obviously we want a different font
        size = 11.0,
        color = NEUTRAL_UNIT_NAME_COLOR,
        borderSize = 0.4,
        borderColor = vector.create(0.0, 0.0, 0.0),
    });

    UI.RegisterTextTemplate("NameplateName",
    {
        font = "Data/Fonts/OpenSans-Medium.ttf", -- Obviously we want a different font
        size = 12.0,
        color = vector.create(240.0/255.0, 240.0/255.0, 240.0/255.0),
        borderSize = 1.0,
        borderColor = vector.create(0.0, 0.0, 0.0),
    });

    UI.RegisterTextTemplate("NameplateHealth",
    {
        font = "Data/Fonts/OpenSans-Regular.ttf", -- Obviously we want a different font
        size = 10.0,
        color = vector.create(240.0/240.0, 240.0/255.0, 240.0/255.0),
        borderSize = 1.0,
        borderColor = vector.create(0.0, 0.0, 0.0),
    });
end

local function SetEnableNameplates(enabled : boolean)
    for i, nameplate in nameplates.entries do
        nameplate:SetEnabled(enabled);
    end
end

-- Setup options
local optionCategory = OptionsContext:GetCategory(OptionCategoryType.Gameplay);
local optionGroup = optionCategory:Get("Nameplates");
local optionSection = optionGroup:Get("General");

-- Enabled option
local enabledOption = optionSection:RegisterOption("Enabled", true, OptionType.Checkbox);
local enabledCancelCallback = enabledOption:RegisterOnValueChanged(function(self, value)
    SetEnableNameplates(value);
    return true;
end);
--enabledCancelCallback(); -- Call this to cancel the callback

-- Increase speed option SLIDER
local increaseSpeedOption = optionSection:RegisterOption("Increase Speed", 10, OptionType.Slider, { Min = 3, Max = 30, Step = 1});
local speedCancelCallback = increaseSpeedOption:RegisterOnValueChanged(function(self, value)
    local number = tonumber(value);
    if (number == nil) then
        return false;
    end

    return true;
end);

-- Text display mode option
local textDisplayModeOption = optionSection:RegisterOption("Text Display Mode", 3, OptionType.ComboBox, {options = { "None", "Val", "Val (%)" }});
local textDisplayModeCancelCallback = textDisplayModeOption:RegisterOnValueChanged(function(self, value)
    local number = tonumber(value);
    if (number == nil) then
        return false;
    end

    return true;
end);

local playerNameTextWidget = nil

local function OnGameLoaded(eventID : number, data : any)
    RegisterTemplates();

    -- Ensure Nameplates Canvas is created
    local canvas : Canvas = UI.GetCanvas("Nameplates", 0, 0, 1920, 1080);
    nameplates.localTargetUnitID = nil;
end

local function OnGameUpdated(eventID : number, data : any)
    local enabled = enabledOption:Get();
    if (not enabled) then
        return;
    end

    for i, nameplate in nameplates.entries do
        local health = nameplate:GetHealth();
        local maxHealth = nameplate:GetMaxHealth();

        if health < maxHealth then
            health += data.deltaTime * increaseSpeedOption:Get();
            health = math.min(health, maxHealth);

            nameplate:SetHealth(health);
        end

        local unitID = nameplate:GetUnitID();
        RefreshUnitNameColor(unitID, nameplate);
        nameplate:SetPosToUnit(unitID);
    end
end

local function OnUnitAdd(eventID : number, data : any)
    local canvas : Canvas = UI.GetCanvas("Nameplates", 0, 0, 1920, 1080);

    local existingNameplate = nameplates.entries[data.unitID];
    if existingNameplate ~= nil then
        existingNameplate:Destroy();
    end

    local nameplate = UINameplate.NewNameplate(canvas, 0, 0, 125, 20, 0, {
        panelTemplate = "DialogBox",
        textTemplate = "PlayerName",

        unitID = data.unitID,
        unitName = Unit.GetName(data.unitID)
    });

    RefreshUnitNameColor(data.unitID, nameplate, true);
    nameplate:SetMaxHealth(50);
    nameplates.entries[data.unitID] = nameplate;
    nameplate:SetPosToUnit(data.unitID);
end

local function OnUnitRemove(eventID : number, data : any)
    local nameplate = nameplates.entries[data.unitID];
    if (nameplate == nil) then
        return
    end

    nameplate:Destroy();
    nameplates.entries[data.unitID] = nil;
    nameplates.deadByUnitID[data.unitID] = nil;
end

local function OnUnitTargetChanged(eventID : number, data : any)
    if Unit.GetLocal() == data.unitID then
        -- Remove highlight from previous target
        if nameplates.localTargetUnitID ~= nil then
            local previousNameplate = nameplates.entries[nameplates.localTargetUnitID];
            if previousNameplate ~= nil then
                previousNameplate:SetHighlighted(false);
            end
        end

        -- Highlight new target
        if data.targetID ~= nil then
            local newNameplate = nameplates.entries[data.targetID];
            if newNameplate ~= nil then
                newNameplate:SetHighlighted(true);
            end
        end

        nameplates.localTargetUnitID = data.targetID;
    end
end

local function OnLocalMoverChanged(eventID : number, data : any)
    local previousMoverUnitID = nameplates.localMoverUnitID;
    nameplates.localMoverUnitID = data.moverID;

    if previousMoverUnitID ~= nil and previousMoverUnitID ~= data.moverID then
        local previousNameplate = nameplates.entries[previousMoverUnitID];
        if previousNameplate ~= nil then
            RefreshUnitNameColor(previousMoverUnitID, previousNameplate, true);
        end
    end

    local localNameplate = nameplates.entries[data.moverID];
    if localNameplate ~= nil then
        RefreshUnitNameColor(data.moverID, localNameplate, true);
    end
end

RegisterEvent(GameEvent.Loaded, OnGameLoaded)
RegisterEvent(GameEvent.Updated, OnGameUpdated)
RegisterEvent(UnitEvent.Add, OnUnitAdd)
RegisterEvent(UnitEvent.Remove, OnUnitRemove)
RegisterEvent(UnitEvent.TargetChanged, OnUnitTargetChanged)
RegisterEvent(GameEvent.LocalMoverChanged, OnLocalMoverChanged)
